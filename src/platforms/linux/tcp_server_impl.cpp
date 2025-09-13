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

#include "../../internal_logger.h"
#include "event_reactor.h"
#include "session_impl.h"

namespace lmshao::network {

const int TCP_BACKLOG = 10;
const int RECV_BUFFER_MAX_SIZE = 4096;

class TcpServerHandler : public EventHandler {
public:
    explicit TcpServerHandler(std::weak_ptr<TcpServerImpl> server) : server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(socket_t fd) override {}

    void HandleError(socket_t fd) override { NETWORK_LOGE("Server socket error on fd: %d", fd); }

    void HandleClose(socket_t fd) override { NETWORK_LOGD("Server socket close on fd: %d", fd); }

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

    void HandleWrite(socket_t fd) override { ProcessSendQueue(); }

    void HandleError(socket_t fd) override
    {
        NETWORK_LOGE("Connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        NETWORK_LOGD("Connection close on fd: %d", fd);
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
    std::weak_ptr<TcpServerImpl> server_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

TcpServerImpl::~TcpServerImpl()
{
    NETWORK_LOGD("fd:%d", socket_);
    Stop();
}

bool TcpServerImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGD("socket error: %s", strerror(errno));
        return false;
    }
    NETWORK_LOGD("init ip: %s, port: %d fd:%d", localIp_.c_str(), localPort_, socket_);

    int optval = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        NETWORK_LOGD("setsockopt error: %s", strerror(errno));
        return false;
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(localPort_);
    inet_aton(localIp_.c_str(), &serverAddr_.sin_addr);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        NETWORK_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    if (listen(socket_, TCP_BACKLOG) < 0) {
        NETWORK_LOGE("listen error: %s", strerror(errno));
        return false;
    }

    taskQueue_ = std::make_unique<TaskQueue>("TcPServerCb");
    return true;
}

bool TcpServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGD("socket not initialized");
        return false;
    }

    taskQueue_->Start();

    serverHandler_ = std::make_shared<TcpServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(serverHandler_)) {
        NETWORK_LOGE("Failed to register server handler");
        return false;
    }

    NETWORK_LOGD("TcpServerImpl started with new EventHandler interface");
    return true;
}

bool TcpServerImpl::Stop()
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

    NETWORK_LOGD("TcpServerImpl stopped");
    return true;
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
{
    if (!data || size == 0) {
        NETWORK_LOGD("invalid data or size");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(size);
    buf->Assign(reinterpret_cast<const char *>(data), size);
    return Send(fd, host, port, buf);
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0) {
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        NETWORK_LOGD("invalid session fd");
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
    NETWORK_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        NETWORK_LOGD("invalid string data");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(fd, host, port, buf);
}

void TcpServerImpl::HandleAccept(socket_t fd)
{
    NETWORK_LOGD("enter");
    struct sockaddr_in clientAddr = {};
    socklen_t addrLen = sizeof(struct sockaddr_in);
    int clientSocket = accept4(fd, (struct sockaddr *)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientSocket < 0) {
        NETWORK_LOGD("accept error: %s", strerror(errno));
        return;
    }

    auto connectionHandler = std::make_shared<TcpConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(connectionHandler)) {
        NETWORK_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    connectionHandlers_[clientSocket] = connectionHandler;

    // EnableKeepAlive(clientSocket);

    std::string host = inet_ntoa(clientAddr.sin_addr);
    uint16_t port = ntohs(clientAddr.sin_port);

    NETWORK_LOGD("New client connections client[%d] %s:%d\n", clientSocket, inet_ntoa(clientAddr.sin_addr),
                 ntohs(clientAddr.sin_port));

    auto session = std::make_shared<SessionImpl>(clientSocket, host, port, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto session = sessions_[clientSocket];
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session]() {
            NETWORK_LOGD("invoke OnAccept callback");
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(session);
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

void TcpServerImpl::HandleReceive(socket_t fd)
{
    NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_unique<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (nbytes > RECV_BUFFER_MAX_SIZE) {
                NETWORK_LOGD("recv %zd bytes", nbytes);
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
            NETWORK_LOGD("recv error: %s(%d)", info.c_str(), errno);

            if (errno == ETIMEDOUT) {
                NETWORK_LOGD("ETIME: connection is timeout");
                break;
            }

            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void TcpServerImpl::EnableKeepAlive(socket_t fd)
{
    int keepAlive = 1;
    constexpr int TCP_KEEP_IDLE = 3;     // Start probing after 3 seconds of no data interaction
    constexpr int TCP_KEEP_INTERVAL = 1; // Probe interval 1 second
    constexpr int TCP_KEEP_COUNT = 2;    // Probe 2 times
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(keepAlive));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &TCP_KEEP_IDLE, sizeof(TCP_KEEP_IDLE));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &TCP_KEEP_INTERVAL, sizeof(TCP_KEEP_INTERVAL));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &TCP_KEEP_COUNT, sizeof(TCP_KEEP_COUNT));
}

void TcpServerImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
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