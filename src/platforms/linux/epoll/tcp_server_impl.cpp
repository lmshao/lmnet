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

#include <atomic>
#include <cerrno>
#include <mutex>
#include <queue>
#include <utility>

#include "event_reactor.h"
#include "internal_logger.h"
#include "tcp_session_impl.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskHandler;

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

    void HandleWrite(socket_t fd) override { ProcessSendQueue(); }

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
        int events = BaseEvents();

        if (writeEventsEnabled_.load(std::memory_order_acquire)) {
            events |= static_cast<int>(EventType::WRITE);
        }

        return events;
    }

    void QueueSend(std::shared_ptr<DataBuffer> buffer)
    {
        if (!buffer || buffer->Size() == 0) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            sendQueue_.push(std::move(buffer));
        }

        if (!EnableWriteEvents()) {
            LMNET_LOGE("Failed to enable connection write events for fd: %d", fd_);
            if (auto server = server_.lock()) {
                server->HandleConnectionClose(fd_, true, "Failed to enable write events");
            }
        }
    }

private:
    bool EnableWriteEvents()
    {
        bool expected = false;
        if (!writeEventsEnabled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                         std::memory_order_acquire)) {
            return true;
        }

        if (EventReactor::GetInstance().ModifyHandler(fd_, GetEvents())) {
            return true;
        }

        writeEventsEnabled_.store(false, std::memory_order_release);
        return false;
    }

    void DisableWriteEvents()
    {
        bool expected = true;
        if (writeEventsEnabled_.compare_exchange_strong(expected, false, std::memory_order_acq_rel,
                                                        std::memory_order_acquire) &&
            !EventReactor::GetInstance().ModifyHandler(fd_, GetEvents())) {
            LMNET_LOGW("Failed to disable connection write events for fd: %d", fd_);
        }
    }

    static int BaseEvents()
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

    void ProcessSendQueue()
    {
        while (true) {
            std::shared_ptr<DataBuffer> buf;
            {
                std::lock_guard<std::mutex> lock(sendMutex_);
                if (sendQueue_.empty()) {
                    break;
                }
                buf = sendQueue_.front();
            }

            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), MSG_NOSIGNAL);
            if (bytesSent > 0) {
                if (static_cast<size_t>(bytesSent) == buf->Size()) {
                    std::lock_guard<std::mutex> lock(sendMutex_);
                    if (!sendQueue_.empty() && sendQueue_.front() == buf) {
                        sendQueue_.pop();
                    }
                } else {
                    auto remaining = DataBuffer::PoolAlloc(buf->Size() - bytesSent);
                    remaining->Assign(buf->Data() + bytesSent, buf->Size() - bytesSent);

                    std::lock_guard<std::mutex> lock(sendMutex_);
                    if (!sendQueue_.empty() && sendQueue_.front() == buf) {
                        sendQueue_.front() = remaining;
                    }
                    continue;
                }
            } else if (bytesSent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                    if (auto server = server_.lock()) {
                        server->HandleConnectionClose(fd_, true, strerror(errno));
                    }
                    return;
                }
            }
        }

        bool queueEmpty = false;
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            queueEmpty = sendQueue_.empty();
        }

        if (queueEmpty) {
            DisableWriteEvents();
        }
    }

private:
    socket_t fd_;
    std::weak_ptr<TcpServerImpl> server_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    std::mutex sendMutex_;
    std::atomic_bool writeEventsEnabled_;
};

TcpServerImpl::~TcpServerImpl()
{
    LMNET_LOGD("fd:%d", socket_);
    Stop();
}

bool TcpServerImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGD("socket error: %s", strerror(errno));
        return false;
    }
    LMNET_LOGD("init ip: %s, port: %d fd:%d", localIp_.c_str(), localPort_, socket_);

    int optval = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        LMNET_LOGD("setsockopt error: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(localPort_);
    inet_aton(localIp_.c_str(), &serverAddr_.sin_addr);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("bind error: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (listen(socket_, TCP_BACKLOG) < 0) {
        LMNET_LOGE("listen error: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (!taskQueue_) {
        taskQueue_ = std::make_unique<TaskQueue>("TcPServerCb");
    }
    return true;
}

bool TcpServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGD("socket not initialized");
        return false;
    }

    if (!taskQueue_) {
        taskQueue_ = std::make_unique<TaskQueue>("TcPServerCb");
    }

    taskQueue_->Start();

    serverHandler_ = std::make_shared<TcpServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(serverHandler_)) {
        LMNET_LOGE("Failed to register server handler");
        serverHandler_.reset();
        taskQueue_->Stop();
        return false;
    }
    serverHandlerRegistered_ = true;

    LMNET_LOGD("TcpServerImpl started with new EventHandler interface");
    return true;
}

bool TcpServerImpl::Stop()
{
    auto &reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        clientFds.reserve(sessions_.size());
        for (const auto &pair : sessions_) {
            clientFds.push_back(pair.first);
        }
        sessions_.clear();
        connectionHandlers_.clear();
    }

    for (int clientFd : clientFds) {
        LMNET_LOGD("close client fd: %d", clientFd);
        reactor.RemoveHandler(clientFd);
        close(clientFd);
    }

    if (socket_ != INVALID_SOCKET) {
        LMNET_LOGD("close server fd: %d", socket_);
        if (serverHandlerRegistered_) {
            reactor.RemoveHandler(socket_);
            serverHandlerRegistered_ = false;
        }
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
    } else {
        serverHandlerRegistered_ = false;
        serverHandler_.reset();
    }

    if (taskQueue_) {
        taskQueue_->Stop();
    }

    LMNET_LOGD("TcpServerImpl stopped");
    return true;
}

bool TcpServerImpl::Send(socket_t fd, const void *data, size_t size)
{
    if (!data || size == 0) {
        LMNET_LOGD("invalid data or size");
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

    std::shared_ptr<TcpConnectionHandler> tcpHandler;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.find(fd) == sessions_.end()) {
            LMNET_LOGD("invalid session fd");
            return false;
        }

        auto handlerIt = connectionHandlers_.find(fd);
        if (handlerIt != connectionHandlers_.end()) {
            tcpHandler = handlerIt->second;
        }
    }

    if (tcpHandler) {
        tcpHandler->QueueSend(buffer);
        return true;
    }
    LMNET_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool TcpServerImpl::Send(socket_t fd, const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGD("invalid string data");
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
    int clientSocket = accept4(fd, (struct sockaddr *)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientSocket < 0) {
        LMNET_LOGD("accept error: %s", strerror(errno));
        return;
    }

    auto connectionHandler = std::make_shared<TcpConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(connectionHandler)) {
        LMNET_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    // EnableKeepAlive(clientSocket);

    std::string host = inet_ntoa(clientAddr.sin_addr);
    uint16_t port = ntohs(clientAddr.sin_port);

    LMNET_LOGD("New client connections client[%d] %s:%d\n", clientSocket, inet_ntoa(clientAddr.sin_addr),
               ntohs(clientAddr.sin_port));

    auto session = std::make_shared<TcpSessionImpl>(clientSocket, host, port, shared_from_this());
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        connectionHandlers_[clientSocket] = connectionHandler;
        sessions_.emplace(clientSocket, session);
    }

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session]() {
            LMNET_LOGD("invoke OnAccept callback");
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(session);
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
                LMNET_LOGD("recv %zd bytes", nbytes);
                break;
            }

            if (!listener_.expired()) {
                auto dataBuffer = std::make_shared<DataBuffer>(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);

                std::shared_ptr<Session> session;
                {
                    std::lock_guard<std::mutex> lock(sessionMutex_);
                    auto it = sessions_.find(fd);
                    if (it != sessions_.end()) {
                        session = it->second;
                    }
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
            LMNET_LOGW("Disconnect fd[%d]", fd);
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN) {
                break;
            }

            std::string info = strerror(errno);
            LMNET_LOGD("recv error: %s(%d)", info.c_str(), errno);

            if (errno == ETIMEDOUT) {
                LMNET_LOGD("ETIME: connection is timeout");
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
    LMNET_LOGD("Closing connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(), isError ? "true" : "false");

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        auto sessionIt = sessions_.find(fd);
        if (sessionIt == sessions_.end()) {
            LMNET_LOGD("Connection fd: %d already cleaned up", fd);
            return;
        }
        session = sessionIt->second;
        sessions_.erase(sessionIt);
        connectionHandlers_.erase(fd);
    }

    EventReactor::GetInstance().RemoveHandler(fd);

    close(fd);

    if (!listener_.expired() && session) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session, reason, isError]() {
            auto listener = listenerWeak.lock();
            if (listener != nullptr) {
                if (isError) {
                    listener->OnError(session, reason);
                }
                listener->OnClose(session);
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    }
}
} // namespace lmshao::lmnet