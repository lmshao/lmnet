/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_server_impl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
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
    TcpServerHandler(socket_t fd, std::weak_ptr<TcpServerImpl> server) : fd_(fd), server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(socket_t fd) override {}

    void HandleError(socket_t fd) override { LMNET_LOGE("Server socket error on fd: %d", fd); }

    void HandleClose(socket_t fd) override { LMNET_LOGD("Server socket close on fd: %d", fd); }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    socket_t fd_;
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
                    LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
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
    LMNET_LOGD("TcpServerImpl destructor");
    Stop();
}

bool TcpServerImpl::Init()
{
    auto is_wildcard = [&](const std::string &s) { return s.empty() || s == "*" || s == "0.0.0.0" || s == "::"; };

    bool want_dual = is_wildcard(localIp_);

    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[16] = {0};
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(localPort_));

    struct addrinfo *result = nullptr;
    int rv = getaddrinfo(want_dual ? nullptr : localIp_.c_str(), port_str, &hints, &result);
    if (rv != 0) {
        LMNET_LOGE("getaddrinfo failed: %s", gai_strerror(rv));
        return false;
    }

    auto set_nonblock = [](int fd) -> bool {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            return false;
        }
        return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
    };

    bool bound_v4 = false;
    bool bound_v6 = false;

    for (struct addrinfo *ai = result; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && !bound_v4) {
            int s = ::socket(AF_INET, SOCK_STREAM, 0);
            if (s < 0) {
                LMNET_LOGE("Failed to create IPv4 socket: %s", strerror(errno));
                continue;
            }

            int reuse = 1;
            if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                LMNET_LOGE("setsockopt SO_REUSEADDR (IPv4) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (!set_nonblock(s)) {
                LMNET_LOGE("fcntl set nonblock (IPv4) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0) {
                LMNET_LOGE("bind (IPv4) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (listen(s, TCP_BACKLOG) < 0) {
                LMNET_LOGE("listen (IPv4) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            ipv4_socket_ = s;
            server_addr4_ = *reinterpret_cast<sockaddr_in *>(ai->ai_addr);
            bound_v4 = true;
        } else if (ai->ai_family == AF_INET6 && !bound_v6) {
            int s = ::socket(AF_INET6, SOCK_STREAM, 0);
            if (s < 0) {
                LMNET_LOGE("Failed to create IPv6 socket: %s", strerror(errno));
                continue;
            }

            int reuse = 1;
            if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
                LMNET_LOGE("setsockopt SO_REUSEADDR (IPv6) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (want_dual) {
                int v6only = 1;
                if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
                    LMNET_LOGE("setsockopt IPV6_V6ONLY failed: %s", strerror(errno));
                }
            }

            if (!set_nonblock(s)) {
                LMNET_LOGE("fcntl set nonblock (IPv6) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0) {
                LMNET_LOGE("bind (IPv6) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            if (listen(s, TCP_BACKLOG) < 0) {
                LMNET_LOGE("listen (IPv6) failed: %s", strerror(errno));
                close(s);
                continue;
            }

            ipv6_socket_ = s;
            server_addr6_ = *reinterpret_cast<sockaddr_in6 *>(ai->ai_addr);
            bound_v6 = true;
        }
    }

    freeaddrinfo(result);

    if (!bound_v4 && !bound_v6) {
        LMNET_LOGE("TCP server init failed: no sockets bound for %s:%u", localIp_.c_str(),
                   static_cast<unsigned>(localPort_));
        return false;
    }

    if (bound_v4 && bound_v6) {
        LMNET_LOGD("TCP server initialized dual-stack on port %u", static_cast<unsigned>(localPort_));
    } else if (bound_v4) {
        char ipbuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &server_addr4_.sin_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("TCP server initialized IPv4 on %s:%u", ipbuf, static_cast<unsigned>(localPort_));
    } else if (bound_v6) {
        char ipbuf[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &server_addr6_.sin6_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("TCP server initialized IPv6 on [%s]:%u", ipbuf, static_cast<unsigned>(localPort_));
    }

    taskQueue_ = std::make_unique<TaskQueue>("TcPServerCb");
    return true;
}

bool TcpServerImpl::Start()
{
    if (ipv4_socket_ == INVALID_SOCKET && ipv6_socket_ == INVALID_SOCKET) {
        LMNET_LOGD("no listening sockets initialized");
        return false;
    }

    taskQueue_->Start();

    auto self = shared_from_this();
    bool ok = true;
    if (ipv4_socket_ != INVALID_SOCKET) {
        ipv4_handler_ = std::make_shared<TcpServerHandler>(ipv4_socket_, self);
        ok = ok && EventReactor::GetInstance().RegisterHandler(ipv4_handler_);
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        ipv6_handler_ = std::make_shared<TcpServerHandler>(ipv6_socket_, self);
        ok = ok && EventReactor::GetInstance().RegisterHandler(ipv6_handler_);
    }
    if (!ok) {
        LMNET_LOGE("Failed to register server handlers");
        if (ipv4_handler_) {
            EventReactor::GetInstance().RemoveHandler(ipv4_socket_);
            ipv4_handler_.reset();
        }
        if (ipv6_handler_) {
            EventReactor::GetInstance().RemoveHandler(ipv6_socket_);
            ipv6_handler_.reset();
        }
        return false;
    }

    LMNET_LOGD("TcpServerImpl started with dual-stack EventHandler");
    return true;
}

bool TcpServerImpl::Stop()
{
    auto &reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
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

    if (ipv4_handler_) {
        LMNET_LOGD("close server IPv4 fd: %d", ipv4_socket_);
        reactor.RemoveHandler(ipv4_socket_);
        ipv4_handler_.reset();
    }
    if (ipv6_handler_) {
        LMNET_LOGD("close server IPv6 fd: %d", ipv6_socket_);
        reactor.RemoveHandler(ipv6_socket_);
        ipv6_handler_.reset();
    }

    if (ipv4_socket_ != INVALID_SOCKET) {
        close(ipv4_socket_);
        ipv4_socket_ = INVALID_SOCKET;
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        close(ipv6_socket_);
        ipv6_socket_ = INVALID_SOCKET;
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

    if (sessions_.find(fd) == sessions_.end()) {
        LMNET_LOGD("invalid session fd");
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
    struct sockaddr_storage client_addr = {};
    socklen_t addr_len = sizeof(client_addr);
    int clientSocket = accept4(fd, (struct sockaddr *)&client_addr, &addr_len, SOCK_NONBLOCK);
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

    connectionHandlers_[clientSocket] = connectionHandler;

    // EnableKeepAlive(clientSocket);

    std::string host;
    uint16_t port = 0;
    if (client_addr.ss_family == AF_INET) {
        auto *sin = reinterpret_cast<struct sockaddr_in *>(&client_addr);
        char ipbuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
        host = ipbuf;
        port = ntohs(sin->sin_port);
    } else if (client_addr.ss_family == AF_INET6) {
        auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(&client_addr);
        char ipbuf[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &sin6->sin6_addr, ipbuf, sizeof(ipbuf));
        host = ipbuf;
        port = ntohs(sin6->sin6_port);
    }

    LMNET_LOGD("New client connections client[%d] %s:%u", clientSocket, host.c_str(), static_cast<unsigned>(port));

    auto session = std::make_shared<TcpSessionImpl>(clientSocket, host, port, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto session = sessions_[clientSocket];
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

    auto sessionIt = sessions_.find(fd);
    if (sessionIt == sessions_.end()) {
        LMNET_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);

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
} // namespace lmshao::lmnet