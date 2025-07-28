//
// Copyright © 2024-2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "tcp_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <queue>

#include "event_reactor.h"
#include "log.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class TcpClientHandler : public EventHandler {
public:
    TcpClientHandler(int fd, std::weak_ptr<TcpClient> client) : fd_(fd), client_(client), writeEventsEnabled_(false) {}

    void HandleRead(int fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(int fd) override { ProcessSendQueue(); }

    void HandleError(int fd) override
    {
        NETWORK_LOGE("Client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(int fd) override
    {
        NETWORK_LOGD("Client connection close on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        int events =
            static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) | static_cast<int>(EventType::CLOSE);

        if (writeEventsEnabled_) {
            events |= static_cast<int>(EventType::WRITE);
        }

        return events;
    }

    void QueueSend(const std::string &data)
    {
        if (data.empty())
            return;
        auto buf = DataBuffer::PoolAlloc(data.size());
        buf->Assign(data.data(), data.size());
        sendQueue_.push(buf);
        EnableWriteEvents();
    }

    void QueueSend(const char *data, size_t size)
    {
        if (!data || size == 0)
            return;
        auto buf = DataBuffer::PoolAlloc(size);
        buf->Assign(data, size);
        sendQueue_.push(buf);
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

private:
    int fd_;
    std::weak_ptr<TcpClient> client_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

TcpClient::TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpClientCb");
}

TcpClient::~TcpClient()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool TcpClient::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("Socket error: %s", strerror(errno));
        return false;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_aton(localIp_.c_str(), &localAddr.sin_addr);

        int optval = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            NETWORK_LOGE("setsockopt SO_REUSEADDR error: %s", strerror(errno));
            return false;
        }

        int ret = bind(socket_, (struct sockaddr *)&localAddr, (socklen_t)sizeof(localAddr));
        if (ret != 0) {
            NETWORK_LOGE("bind error: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

void TcpClient::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
    Init();
}

bool TcpClient::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    if (remoteIp_.empty()) {
        remoteIp_ = "127.0.0.1";
    }

    inet_aton(remoteIp_.c_str(), &serverAddr_.sin_addr);

    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        NETWORK_LOGE("connect(%s:%d) failed: %s", remoteIp_.c_str(), remotePort_, strerror(errno));
        ReInit();
        return false;
    }

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(socket_, &writefds);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    ret = select(socket_ + 1, NULL, &writefds, NULL, &timeout);
    if (ret > 0) {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            NETWORK_LOGE("getsockopt error, %s", strerror(errno));
            ReInit();
            return false;
        }

        if (error != 0) {
            NETWORK_LOGE("connect error, %s", strerror(errno));
            ReInit();
            return false;
        }
    } else {
        NETWORK_LOGE("connect error, %s", strerror(errno));
        ReInit();
        return false;
    }

    taskQueue_->Start();

    clientHandler_ = std::make_shared<TcpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(clientHandler_)) {
        NETWORK_LOGE("Failed to register client handler");
        return false;
    }

    NETWORK_LOGD("Connect (%s:%d) success with new EventHandler interface.", remoteIp_.c_str(), remotePort_);
    return true;
}

bool TcpClient::Send(const std::string &str)
{
    return Send(str.c_str(), str.length());
}

bool TcpClient::Send(const char *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    if (clientHandler_) {
        clientHandler_->QueueSend(data, len);
        return true;
    }

    NETWORK_LOGE("Client handler not found");
    return false;
}

bool TcpClient::Send(std::shared_ptr<DataBuffer> data)
{
    return Send(reinterpret_cast<const char *>(data->Data()), data->Size());
}

void TcpClient::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

void TcpClient::HandleReceive(int fd)
{
    NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), 0);

        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);

                auto task = std::make_shared<TaskHandler<void>>([dataBuffer, fd, this]() {
                    auto listener = listener_.lock();
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
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // Usually same value, but check both for portability
                // Normal case: no data available to read, return directly
                break;
            }

            std::string info = strerror(errno);
            NETWORK_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void TcpClient::HandleConnectionClose(int fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
                 isError ? "true" : "false");

    if (socket_ != fd) {
        NETWORK_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance()->RemoveHandler(fd);
    close(fd);
    socket_ = INVALID_SOCKET;
    clientHandler_.reset();

    if (!listener_.expired()) {
        auto task = std::make_shared<TaskHandler<void>>([reason, isError, fd, this]() {
            auto listener = listener_.lock();
            if (listener != nullptr) {
                if (isError) {
                    listener->OnError(fd, reason);
                } else {
                    listener->OnClose(fd);
                }
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    }
}