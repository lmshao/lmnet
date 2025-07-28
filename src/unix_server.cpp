//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "unix_server.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>

#include "event_reactor.h"
#include "log.h"
#include "session_impl.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class UnixServerHandler : public EventHandler {
public:
    explicit UnixServerHandler(std::weak_ptr<UnixServer> server) : server_(server) {}
    void HandleRead(int fd) override
    {
        if (auto s = server_.lock())
            s->HandleAccept(fd);
    }
    void HandleWrite(int) override {}

    void HandleError(int fd) override { NETWORK_LOGE("UNIX server socket error on fd: %d", fd); }

    void HandleClose(int fd) override { NETWORK_LOGD("UNIX server socket close on fd: %d", fd); }

    int GetHandle() const override
    {
        if (auto s = server_.lock()) {
            return s->GetSocketFd();
        }
        return -1;
    }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    std::weak_ptr<UnixServer> server_;
};

class UnixConnectionHandler : public EventHandler {
public:
    UnixConnectionHandler(int fd, std::weak_ptr<UnixServer> server)
        : fd_(fd), server_(server), writeEventsEnabled_(false)
    {
    }

    void HandleRead(int fd) override
    {
        if (auto s = server_.lock())
            s->HandleReceive(fd);
    }

    void HandleWrite(int fd) override { ProcessSendQueue(); }

    void HandleError(int fd) override
    {
        NETWORK_LOGE("UNIX connection error on fd: %d", fd);
        if (auto s = server_.lock())
            s->HandleConnectionClose(fd, true, "Connection error");
    }

    void HandleClose(int fd) override
    {
        NETWORK_LOGD("UNIX connection close on fd: %d", fd);
        if (auto s = server_.lock())
            s->HandleConnectionClose(fd, false, "Connection closed");
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

    void QueueSend(const std::string &data)
    {
        if (data.empty())
            return;
        sendQueue_.push(data);
        EnableWriteEvents();
    }

    void QueueSend(const char *data, size_t size)
    {
        if (!data || size == 0)
            return;
        sendQueue_.push(std::string(data, size));
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
            const std::string &data = sendQueue_.front();
            ssize_t bytesSent = send(fd_, data.c_str(), data.size(), MSG_NOSIGNAL);
            if (bytesSent > 0) {
                if (static_cast<size_t>(bytesSent) == data.size()) {
                    sendQueue_.pop();
                } else {
                    std::string remaining = data.substr(bytesSent);
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
    std::weak_ptr<UnixServer> server_;
    std::queue<std::string> sendQueue_;
    bool writeEventsEnabled_;
};

UnixServer::UnixServer(const std::string &socketPath) : socketPath_(socketPath) {}

UnixServer::~UnixServer()
{
    Stop();
}

bool UnixServer::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket error: %s", strerror(errno));
        return false;
    }
    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);
    unlink(socketPath_.c_str());

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        NETWORK_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    if (listen(socket_, 10) < 0) {
        NETWORK_LOGE("listen error: %s", strerror(errno));
        return false;
    }
    taskQueue_ = std::make_unique<TaskQueue>("UnixServerCb");
    return true;
}

bool UnixServer::Start()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }
    taskQueue_->Start();
    serverHandler_ = std::make_shared<UnixServerHandler>(shared_from_this());

    if (!EventReactor::GetInstance()->RegisterHandler(std::static_pointer_cast<EventHandler>(serverHandler_))) {
        NETWORK_LOGE("Failed to register UNIX server handler");
        return false;
    }

    NETWORK_LOGD("UnixServer started with new EventHandler interface");
    return true;
}

bool UnixServer::Stop()
{
    if (socket_ != INVALID_SOCKET && serverHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
        unlink(socketPath_.c_str());
    }

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }

    NETWORK_LOGD("UnixServer stopped");
    return true;
}

bool UnixServer::Send(int fd, std::string host, uint16_t port, const void *data, size_t size)
{
    (void)host;
    (void)port;
    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            unixHandler->QueueSend(reinterpret_cast<const char *>(data), size);
            return true;
        }
    }
    NETWORK_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool UnixServer::Send(int fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer)
        return false;
    return Send(fd, host, port, buffer->Data(), buffer->Size());
}

bool UnixServer::Send(int fd, std::string host, uint16_t port, const std::string &str)
{
    return Send(fd, host, port, str.data(), str.size());
}

bool UnixServer::Send(int fd, const std::string &str)
{
    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            unixHandler->QueueSend(str);
            return true;
        }
    }
    NETWORK_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool UnixServer::Send(int fd, std::shared_ptr<DataBuffer> buffer)
{
    return Send(fd, std::string(reinterpret_cast<const char *>(buffer->Data()), buffer->Size()));
}

void UnixServer::Close()
{
    Stop();
}

void UnixServer::HandleAccept(int fd)
{
    struct sockaddr_un clientAddr = {};
    socklen_t addrLen = sizeof(clientAddr);
    int clientSocket = accept4(fd, (struct sockaddr *)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientSocket < 0) {
        NETWORK_LOGE("accept error: %s", strerror(errno));
        return;
    }

    auto connectionHandler = std::make_shared<UnixConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(std::static_pointer_cast<EventHandler>(connectionHandler))) {
        NETWORK_LOGE("Failed to register connection handler");
        close(clientSocket);
        return;
    }
    connectionHandlers_[clientSocket] = connectionHandler;

    auto session = std::make_shared<SessionImpl>(clientSocket, "local", 0, shared_from_this());
    sessions_.emplace(clientSocket, session);
    if (!listener_.expired()) {
        auto listener = listener_.lock();
        if (listener) {
            listener->OnAccept(session);
        }
    }
}

void UnixServer::HandleReceive(int fd)
{
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), 0);
        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);
                auto sessionIt = sessions_.find(fd);
                std::shared_ptr<Session> session = (sessionIt != sessions_.end()) ? sessionIt->second : nullptr;
                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>([listenerWeak, dataBuffer, session]() {
                    auto listener = listenerWeak.lock();
                    if (listener && session) {
                        listener->OnReceive(session, dataBuffer);
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

void UnixServer::HandleConnectionClose(int fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing UNIX connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
                 isError ? "true" : "false");
    auto sessionIt = sessions_.find(fd);
    if (sessionIt == sessions_.end()) {
        NETWORK_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }
    EventReactor::GetInstance()->RemoveHandler(fd);
    close(fd);

    std::shared_ptr<Session> session = sessionIt->second;
    sessions_.erase(sessionIt);
    connectionHandlers_.erase(fd);
    if (!listener_.expired() && session) {
        auto listener = listener_.lock();
        if (isError) {
            listener->OnError(session, reason);
        } else {
            listener->OnClose(session);
        }
    }
}
