/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */
#include "tcp_server_impl.h"

#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <queue>
#include <vector>

#include "event_reactor.h"
#include "internal_logger.h"
#include "socket_utils.h"
#include "tcp_session_impl.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;
using namespace darwin;

namespace {
const int TCP_BACKLOG = 10;
const int RECV_BUFFER_MAX_SIZE = 4096;

bool SetSocketOptions(socket_t fd)
{
    if (!ConfigureAcceptedSocket(fd)) {
        return false;
    }
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        LMNET_LOGW("setsockopt SO_REUSEADDR failed on fd %d: %s", fd, strerror(errno));
    }
    return true;
}

} // namespace

class TcpServerHandler : public EventHandler {
public:
    explicit TcpServerHandler(std::weak_ptr<TcpServerImpl> server) : server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(socket_t) override {}

    void HandleError(socket_t fd) override { LMNET_LOGE("Server socket error on fd: %d", fd); }

    void HandleClose(socket_t fd) override { LMNET_LOGD("Server socket close on fd: %d", fd); }

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
    std::weak_ptr<TcpServerImpl> server_;
};

class TcpConnectionHandler : public EventHandler {
public:
    TcpConnectionHandler(socket_t fd, std::weak_ptr<TcpServerImpl> server)
        : fd_(fd), server_(server), writeEventsEnabled_(false)
    {
    }

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t) override { ProcessSendQueue(); }

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("Connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("Connection close on fd: %d", fd);
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
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
        }
    }

    void DisableWriteEvents()
    {
        if (writeEventsEnabled_) {
            writeEventsEnabled_ = false;
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
        }
    }

    void ProcessSendQueue()
    {
        while (!sendQueue_.empty()) {
            auto &buf = sendQueue_.front();
            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), 0);

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
                }
                LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                return;
            }
        }

        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }

private:
    socket_t fd_;
    std::weak_ptr<TcpServerImpl> server_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

TcpServerImpl::~TcpServerImpl()
{
    LMNET_LOGD("fd:%d", socket_);
    Stop();
}

bool TcpServerImpl::Init()
{
    socket_ = CreateStreamSocket(AF_INET);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket error: %s", strerror(errno));
        return false;
    }

    if (!SetSocketOptions(socket_)) {
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    LMNET_LOGD("init ip: %s, port: %d fd:%d", localIp_.c_str(), localPort_, socket_);

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(localPort_);
    inet_aton(localIp_.c_str(), &serverAddr_.sin_addr);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    if (listen(socket_, TCP_BACKLOG) < 0) {
        LMNET_LOGE("listen error: %s", strerror(errno));
        return false;
    }

    taskQueue_ = std::make_unique<TaskQueue>("TcpServerCb");

    return true;
}

bool TcpServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized");
        return false;
    }

    taskQueue_->Start();

    serverHandler_ = std::make_shared<TcpServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(serverHandler_)) {
        LMNET_LOGE("Failed to register server handler");
        return false;
    }

    LMNET_LOGD("TcpServerImpl started with kqueue backend");
    return true;
}

bool TcpServerImpl::Stop()
{
    auto &reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
    clientFds.reserve(sessions_.size());
    for (const auto &pair : sessions_) {
        clientFds.push_back(pair.first);
    }

    for (int clientFd : clientFds) {
        LMNET_LOGD("close client fd: %d", clientFd);
        reactor.RemoveHandler(clientFd);
        close(clientFd);
        connectionHandlers_.erase(clientFd);
    }
    sessions_.clear();

    if (socket_ != INVALID_SOCKET && serverHandler_) {
        LMNET_LOGD("close server fd: %d", socket_);
        reactor.RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
    }

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }

    LMNET_LOGD("TcpServerImpl stopped");
    return true;
}

bool TcpServerImpl::Send(socket_t fd, const void *data, size_t size)
{
    if (!data || size == 0) {
        LMNET_LOGE("invalid data or size");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(size);
    buf->Assign(reinterpret_cast<const char *>(data), size);
    return Send(fd, std::move(buf));
}

bool TcpServerImpl::Send(socket_t fd, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0) {
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        LMNET_LOGE("invalid session fd");
        return false;
    }

    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto tcpHandler = handlerIt->second;
        if (tcpHandler) {
            tcpHandler->QueueSend(buffer);
            return true;
        }
    }
    LMNET_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool TcpServerImpl::Send(socket_t fd, const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("invalid string data");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(fd, std::move(buf));
}

void TcpServerImpl::HandleAccept(socket_t fd)
{
    LMNET_LOGD("enter");
    struct sockaddr_in clientAddr = {};
    socklen_t addrLen = sizeof(struct sockaddr_in);
    int clientSocket = accept(fd, (struct sockaddr *)&clientAddr, &addrLen);
    if (clientSocket < 0) {
        LMNET_LOGE("accept error: %s", strerror(errno));
        return;
    }

    if (!ConfigureAcceptedSocket(clientSocket)) {
        LMNET_LOGE("Failed to configure client socket: %s", strerror(errno));
        close(clientSocket);
        return;
    }

    auto connectionHandler = std::make_shared<TcpConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(connectionHandler)) {
        LMNET_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    connectionHandlers_[clientSocket] = connectionHandler;

    std::string host = inet_ntoa(clientAddr.sin_addr);
    uint16_t port = ntohs(clientAddr.sin_port);

    LMNET_LOGD("New client connections client[%d] %s:%d", clientSocket, host.c_str(), port);

    auto session = std::make_shared<TcpSessionImpl>(clientSocket, host, port, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto sessionPtr = sessions_[clientSocket];
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, sessionPtr]() {
            LMNET_LOGD("invoke OnAccept callback");
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(sessionPtr);
            } else {
                LMNET_LOGD("not found listener!");
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    } else {
        LMNET_LOGD("listener is null");
    }
}

void TcpServerImpl::HandleReceive(socket_t fd)
{
    LMNET_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_unique<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (nbytes > RECV_BUFFER_MAX_SIZE) {
                LMNET_LOGE("recv %zd bytes", nbytes);
                break;
            }

            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
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
                        if (listener) {
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
            LMNET_LOGW("Disconnect fd[%d]", fd);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            std::string info = strerror(errno);
            LMNET_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void TcpServerImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing server connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
               isError ? "true" : "false");

    auto sessionIt = sessions_.find(fd);
    if (sessionIt == sessions_.end()) {
        LMNET_LOGD("Session fd %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);
    close(fd);

    connectionHandlers_.erase(fd);
    auto session = sessionIt->second;
    sessions_.erase(sessionIt);

    if (!listener_.expired()) {
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

void TcpServerImpl::EnableKeepAlive(socket_t fd)
{
    int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable)) == -1) {
        LMNET_LOGW("setsockopt SO_KEEPALIVE failed: %s", strerror(errno));
        return;
    }

    int idle = 60;
    if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle)) == -1) {
        LMNET_LOGW("setsockopt TCP_KEEPALIVE failed: %s", strerror(errno));
    }
}

} // namespace lmshao::lmnet
