/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include "unix_server_impl.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>

#include "event_reactor.h"
#include "network/network_logger.h"
#include "session_impl.h"

namespace lmshao::network {

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class UnixServerHandler : public EventHandler {
public:
    explicit UnixServerHandler(std::weak_ptr<UnixServerImpl> server) : server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(int) override {}

    void HandleError(socket_t fd) override { NETWORK_LOGE("Unix server socket error on fd: %d", fd); }

    void HandleClose(socket_t fd) override { NETWORK_LOGD("Unix server socket close on fd: %d", fd); }

    int GetHandle() const override
    {
        if (auto server = server_.lock()) {
            return server->GetSocketFd();
        }
        return -1;
    }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    std::weak_ptr<UnixServerImpl> server_;
};

class UnixConnectionHandler : public EventHandler {
public:
    UnixConnectionHandler(socket_t fd, std::weak_ptr<UnixServerImpl> server)
        : fd_(fd), server_(server), writeEventsEnabled_(false)
    {
    }

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t fd) override { ProcessSendQueue(); }

    void HandleError(socket_t fd) override
    {
        NETWORK_LOGE("Unix connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        NETWORK_LOGD("Unix connection close on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, false, "Connection closed");
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

    void QueueSend(std::shared_ptr<DataBuffer> buffer)
    {
        if (!buffer || buffer->Size() == 0) {
            return;
        }
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

private:
    socket_t fd_;
    std::weak_ptr<UnixServerImpl> server_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

UnixServerImpl::UnixServerImpl(const std::string &socketPath) : socketPath_(socketPath) {}

UnixServerImpl::~UnixServerImpl()
{
    NETWORK_LOGD("fd:%d", socket_);
    Stop();
}

bool UnixServerImpl::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket error: %s", strerror(errno));
        return false;
    }
    NETWORK_LOGD("init path: %s, fd:%d", socketPath_.c_str(), socket_);

    // Remove existing socket file if it exists
    unlink(socketPath_.c_str());

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);

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

bool UnixServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    taskQueue_->Start();

    serverHandler_ = std::make_shared<UnixServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(serverHandler_)) {
        NETWORK_LOGE("Failed to register server handler");
        return false;
    }

    NETWORK_LOGD("UnixServerImpl started with new EventHandler interface");
    return true;
}

bool UnixServerImpl::Stop()
{
    auto reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
    for (const auto &pair : sessions_) {
        clientFds.push_back(pair.first);
    }

    for (int clientFd : clientFds) {
        NETWORK_LOGD("close client fd: %d", clientFd);
        reactor->RemoveHandler(clientFd);
        close(clientFd);

        connectionHandlers_.erase(clientFd);
    }
    sessions_.clear();

    if (socket_ != INVALID_SOCKET && serverHandler_) {
        NETWORK_LOGD("close server fd: %d", socket_);
        reactor->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
    }

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }

    // Remove socket file
    unlink(socketPath_.c_str());

    NETWORK_LOGD("UnixServerImpl stopped");
    return true;
}

bool UnixServerImpl::Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
{
    if (!data || size == 0) {
        NETWORK_LOGE("invalid data or size");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(size);
    buf->Assign(reinterpret_cast<const char *>(data), size);
    return Send(fd, host, port, buf);
}

bool UnixServerImpl::Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0) {
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        NETWORK_LOGE("invalid session fd");
        return false;
    }

    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            unixHandler->QueueSend(buffer);
            return true;
        }
    }
    NETWORK_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool UnixServerImpl::Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        NETWORK_LOGE("invalid string data");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(fd, host, port, buf);
}

void UnixServerImpl::HandleAccept(socket_t fd)
{
    NETWORK_LOGD("enter");
    struct sockaddr_un clientAddr = {};
    socklen_t addrLen = sizeof(struct sockaddr_un);
    int clientSocket = accept4(fd, (struct sockaddr *)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientSocket < 0) {
        NETWORK_LOGE("accept error: %s", strerror(errno));
        return;
    }

    auto connectionHandler = std::make_shared<UnixConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(connectionHandler)) {
        NETWORK_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    connectionHandlers_[clientSocket] = connectionHandler;

    NETWORK_LOGD("New Unix client connection client[%d]\n", clientSocket);

    // Unix domain socket uses empty host and port
    auto session = std::make_shared<SessionImpl>(clientSocket, socketPath_, 0, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto sessionPtr = sessions_[clientSocket];
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, sessionPtr]() {
            NETWORK_LOGD("invoke OnAccept callback");
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(sessionPtr);
            } else {
                NETWORK_LOGD("not found listener!");
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    } else {
        NETWORK_LOGD("listener is null");
    }
}

void UnixServerImpl::HandleReceive(socket_t fd)
{
    NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (nbytes > RECV_BUFFER_MAX_SIZE) {
                NETWORK_LOGE("recv %zd bytes", nbytes);
                break;
            }

            if (!listener_.expired()) {
                auto dataBuffer = std::make_shared<DataBuffer>(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);

                std::shared_ptr<Session> session;
                auto it = sessions_.find(fd);
                if (it != sessions_.end()) {
                    session = it->second;
                }

                if (session) {
                    auto listenerWeak = listener_;
                    auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session, dataBuffer]() {
                        auto listener = listenerWeak.lock();
                        if (listener != nullptr) {
                            listener->OnReceive(session, dataBuffer);
                        }
                    });
                    if (taskQueue_) {
                        taskQueue_->EnqueueTask(task);
                    }
                }
            }
            continue;
        } else if (nbytes == 0) {
            NETWORK_LOGW("Disconnect fd[%d]", fd);
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN) {
                break;
            }

            std::string info = strerror(errno);
            NETWORK_LOGE("recv error: %s(%d)", info.c_str(), errno);

            if (errno == ETIMEDOUT) {
                NETWORK_LOGE("ETIME: connection is timeout");
                break;
            }

            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void UnixServerImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(), isError ? "true" : "false");

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
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session, reason, isError]() {
            auto listener = listenerWeak.lock();
            if (listener != nullptr) {
                if (isError) {
                    listener->OnError(session, reason);
                } else {
                    listener->OnClose(session);
                }
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    }
}
} // namespace lmshao::network
