//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "unix_client.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>

#include "event_reactor.h"
#include "log.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class UnixClientHandler : public EventHandler {
public:
    UnixClientHandler(int fd, std::weak_ptr<UnixClient> client) : fd_(fd), client_(client), writeEventsEnabled_(false)
    {
    }
    void HandleRead(int fd) override
    {
        if (auto c = client_.lock()) {
            c->HandleReceive(fd);
        }
    }
    void HandleWrite(int fd) override { ProcessSendQueue(); }
    void HandleError(int fd) override
    {
        NETWORK_LOGE("UNIX client connection error on fd: %d", fd);
        if (auto c = client_.lock()) {
            c->HandleConnectionClose(fd, true, "Connection error");
        }
    }
    void HandleClose(int fd) override
    {
        NETWORK_LOGD("UNIX client connection close on fd: %d", fd);
        if (auto c = client_.lock()) {
            c->HandleConnectionClose(fd, false, "Connection closed");
        }
    }
    int GetHandle() const override { return fd_; }
    int GetEvents() const override
    {
        int events =
            static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) | static_cast<int>(EventType::CLOSE);
        if (writeEventsEnabled_)
            events |= static_cast<int>(EventType::WRITE);
        return events;
    }
    void QueueSend(std::shared_ptr<DataBuffer> buffer)
    {
        if (!buffer || buffer->Size() == 0)
            return;
        sendQueue_.push(buffer);
        EnableWriteEvents();
    }

private:
    void EnableWriteEvents()
    {
        if (!writeEventsEnabled_) {
            writeEventsEnabled_ = true;
            EventReactor::GetInstance()->ModifyHandler(fd_, GetEvents());
        }
    }
    void DisableWriteEvents()
    {
        if (writeEventsEnabled_) {
            writeEventsEnabled_ = false;
            EventReactor::GetInstance()->ModifyHandler(fd_, GetEvents());
        }
    }
    void ProcessSendQueue()
    {
        while (!sendQueue_.empty()) {
            auto &buf = sendQueue_.front();
            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), MSG_NOSIGNAL);
            if (bytesSent > 0) {
                if (static_cast<size_t>(bytesSent) == buf->Size()) {
                    sendQueue_.pop();
                } else {
                    auto remaining = DataBuffer::PoolAlloc(buf->Size() - bytesSent);
                    remaining->Assign(buf->Data() + bytesSent, buf->Size() - bytesSent);
                    sendQueue_.front() = remaining;
                    break;
                }
            } else if (bytesSent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    NETWORK_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                    return;
                }
            }
        }
        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }
    int fd_;
    std::weak_ptr<UnixClient> client_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

UnixClient::UnixClient(const std::string &socketPath) : socketPath_(socketPath) {}
UnixClient::~UnixClient()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool UnixClient::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket error: %s", strerror(errno));
        return false;
    }
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);
    taskQueue_ = std::make_unique<TaskQueue>("UnixClientCb");
    return true;
}

bool UnixClient::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }
    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        NETWORK_LOGE("connect(%s) failed: %s", socketPath_.c_str(), strerror(errno));
        return false;
    }
    taskQueue_->Start();
    clientHandler_ = std::make_shared<UnixClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(std::static_pointer_cast<EventHandler>(clientHandler_))) {
        NETWORK_LOGE("Failed to register UNIX client handler");
        return false;
    }
    NETWORK_LOGD("UnixClient connected with new EventHandler interface.");
    return true;
}

bool UnixClient::Send(const std::string &str)
{
    if (str.empty()) {
        NETWORK_LOGE("Invalid string for Send");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(buf);
}

bool UnixClient::Send(const void *data, size_t len)
{
    if (data == nullptr || len == 0) {
        NETWORK_LOGE("Invalid data or length for Send");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(len);
    buf->Assign(reinterpret_cast<const char *>(data), len);
    return Send(buf);
}

bool UnixClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }
    if (clientHandler_) {
        clientHandler_->QueueSend(data);
        return true;
    }
    NETWORK_LOGE("Client handler not found");
    return false;
}

void UnixClient::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

void UnixClient::HandleReceive(int fd)
{
    NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }
    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);
        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);
                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>([listenerWeak, dataBuffer, fd]() {
                    auto listener = listenerWeak.lock();
                    if (listener) {
                        listener->OnReceive(fd, dataBuffer);
                    }
                });
                if (taskQueue_) {
                    taskQueue_->EnqueueTask(task);
                }
            }
            continue;
        } else if (nbytes == 0) {
            NETWORK_LOGW("Disconnect fd[%d]", fd);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            std::string info = strerror(errno);
            NETWORK_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }
        break;
    }
}

void UnixClient::HandleConnectionClose(int fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing UNIX client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
                 isError ? "true" : "false");
    EventReactor::GetInstance()->RemoveHandler(fd);
    close(fd);
    socket_ = INVALID_SOCKET;
    clientHandler_.reset();
    if (!listener_.expired()) {
        auto listener = listener_.lock();
        if (isError) {
            listener->OnError(fd, reason);
        } else {
            listener->OnClose(fd);
        }
    }
}
