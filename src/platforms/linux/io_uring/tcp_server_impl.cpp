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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "io_uring_session_impl.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskHandler;

TcpServerImpl::TcpServerImpl(std::string local_ip, uint16_t local_port)
    : localIp_(std::move(local_ip)), localPort_(local_port), taskQueue_(std::make_unique<TaskQueue>("tcp_server_tq"))
{
    taskQueue_->Start();
}

TcpServerImpl::~TcpServerImpl()
{
    if (isRunning_) {
        Stop();
    }
    taskQueue_->Stop();
}

bool TcpServerImpl::Init()
{
    // Resolve local address for dual-stack
    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // allow wildcard when localIp_ empty

    char port_str[16] = {0};
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(localPort_));

    struct addrinfo *res = nullptr;
    // Treat common wildcard representations as dual-stack wildcard
    bool want_dual = localIp_.empty() || localIp_ == "0.0.0.0" || localIp_ == "::" || localIp_ == "*";
    int gai = getaddrinfo(want_dual ? nullptr : localIp_.c_str(), port_str, &hints, &res);
    if (gai != 0) {
        LMNET_LOGE("getaddrinfo failed: %s", gai_strerror(gai));
        return false;
    }

    bool any_success = false;
    for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET && ai->ai_family != AF_INET6) {
            continue;
        }

        int s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) {
            LMNET_LOGW("Failed to create socket (family=%d): %s", ai->ai_family, strerror(errno));
            continue;
        }

        int opt = 1;
        if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            LMNET_LOGW("Failed to set SO_REUSEADDR: %s", strerror(errno));
        }

        if (ai->ai_family == AF_INET6) {
            // Ensure separate IPv6-only socket (no v4-mapped)
            int v6only = 1;
            if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
                LMNET_LOGW("Failed to set IPV6_V6ONLY: %s", strerror(errno));
            }
        }

        if (bind(s, ai->ai_addr, ai->ai_addrlen) < 0) {
            LMNET_LOGW("Failed to bind socket: %s", strerror(errno));
            close(s);
            continue;
        }

        if (listen(s, SOMAXCONN) < 0) {
            LMNET_LOGW("Failed to listen on socket: %s", strerror(errno));
            close(s);
            continue;
        }

        if (ai->ai_family == AF_INET) {
            ipv4_socket_ = s;
        } else if (ai->ai_family == AF_INET6) {
            ipv6_socket_ = s;
        }
        any_success = true;
    }

    freeaddrinfo(res);

    if (!any_success) {
        LMNET_LOGE("No listening sockets created for %s:%u", localIp_.c_str(), localPort_);
        return false;
    }

    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        if (ipv4_socket_ != INVALID_SOCKET) {
            close(ipv4_socket_);
            ipv4_socket_ = INVALID_SOCKET;
        }
        if (ipv6_socket_ != INVALID_SOCKET) {
            close(ipv6_socket_);
            ipv6_socket_ = INVALID_SOCKET;
        }
        return false;
    }

    isRunning_ = true;
    LMNET_LOGI("TCP server initialized on %s:%u (v4_fd=%d, v6_fd=%d)", localIp_.c_str(), localPort_, ipv4_socket_,
               ipv6_socket_);
    return true;
}

bool TcpServerImpl::Start()
{
    if (!isRunning_) {
        LMNET_LOGW("Server is not initialized or has been stopped.");
        return false;
    }
    if (ipv4_socket_ != INVALID_SOCKET) {
        SubmitAcceptOnSocket(ipv4_socket_);
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        SubmitAcceptOnSocket(ipv6_socket_);
    }
    LMNET_LOGI("TCP server started.");
    return true;
}

bool TcpServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }

    LMNET_LOGI("Stopping TCP server...");

    // Close listening sockets first to prevent new connections
    if (ipv4_socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(ipv4_socket_, nullptr);
        ipv4_socket_ = INVALID_SOCKET;
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(ipv6_socket_, nullptr);
        ipv6_socket_ = INVALID_SOCKET;
    }

    // Disconnect all active sessions
    std::unique_lock<std::mutex> lock(sessionMutex_);
    for (auto const &[fd, session] : sessions_) {
        Disconnect(fd);
    }
    sessions_.clear();
    lock.unlock();

    LMNET_LOGI("TCP server stopped.");
    return true;
}

void TcpServerImpl::SubmitAcceptOnSocket(socket_t listen_fd)
{
    if (!isRunning_)
        return;

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitAcceptRequest(
        listen_fd, [self, listen_fd](int fd, int client_fd, const sockaddr *addr, socklen_t *addrlen) {
            if (client_fd >= 0 && addr && addrlen) {
                sockaddr_storage addr_copy{};
                socklen_t len = *addrlen;
                memcpy(&addr_copy, addr, std::min(len, (socklen_t)sizeof(addr_copy)));
                self->HandleAccept(client_fd, addr_copy, len);
            } else if (client_fd < 0) {
                LMNET_LOGE("Accept failed on fd %d: %s", fd, strerror(-client_fd));
            }

            // Resubmit for next connection on the same listening fd
            if (self->isRunning_) {
                self->SubmitAcceptOnSocket(listen_fd);
            }
        });
}

void TcpServerImpl::HandleAccept(int res, const sockaddr_storage &client_addr, socklen_t addrlen)
{
    if (res >= 0) {
        int client_fd = res;
        char ip_str[INET6_ADDRSTRLEN] = {0};
        uint16_t client_port = 0;

        if (client_addr.ss_family == AF_INET) {
            const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(&client_addr);
            inet_ntop(AF_INET, &in->sin_addr, ip_str, sizeof(ip_str));
            client_port = ntohs(in->sin_port);
        } else if (client_addr.ss_family == AF_INET6) {
            const sockaddr_in6 *in6 = reinterpret_cast<const sockaddr_in6 *>(&client_addr);
            inet_ntop(AF_INET6, &in6->sin6_addr, ip_str, sizeof(ip_str));
            client_port = ntohs(in6->sin6_port);
        } else {
            strncpy(ip_str, "unknown", sizeof(ip_str) - 1);
        }

        auto session = std::make_shared<IoUringSessionImpl>(client_fd, std::string(ip_str), client_port);

        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessions_[client_fd] = session;
        }

        LMNET_LOGI("Accepted new connection from %s:%u on fd %d", ip_str, client_port, client_fd);

        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session] {
            if (listener) {
                listener->OnAccept(session);
            }
        });
        taskQueue_->EnqueueTask(task);

        SubmitRead(session);
    } else {
        if (isRunning_) {
            LMNET_LOGE("Accept failed: %s", strerror(-res));
        }
    }
}

void TcpServerImpl::SubmitRead(std::shared_ptr<Session> session)
{
    if (!isRunning_ || !session)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitReadRequest(
        session->fd, buffer, [self, session](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read) {
            self->HandleReceive(session, buf, bytes_read);
        });
}

void TcpServerImpl::HandleReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer, int bytes_read)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);

        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session, buffer] {
            if (listener) {
                listener->OnReceive(session, buffer);
            }
        });
        taskQueue_->EnqueueTask(task);

        SubmitRead(session); // Continue reading from the client
    } else if (bytes_read == 0) {
        HandleConnectionClose(session->fd, "Connection closed by peer");
    } else {
        HandleConnectionClose(session->fd, std::string("Read error: ") + strerror(-bytes_read));
    }
}

void TcpServerImpl::HandleConnectionClose(int client_fd, const std::string &reason)
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.count(client_fd)) {
            session = sessions_[client_fd];
            sessions_.erase(client_fd);
        }
    }

    if (session) {
        LMNET_LOGI("Connection closed for fd %d: %s", client_fd, reason.c_str());
        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session] {
            if (listener) {
                listener->OnClose(session);
            }
        });
        taskQueue_->EnqueueTask(task);
    }
}

void TcpServerImpl::Disconnect(socket_t fd)
{
    if (!isRunning_)
        return;

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.find(fd) == sessions_.end()) {
            LMNET_LOGW("Attempted to disconnect an unknown session (fd: %d)", fd);
            return;
        }
        session = sessions_[fd];
    }

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitCloseRequest(
        fd, [self, fd](int, int) { self->HandleConnectionClose(fd, "Disconnected by server"); });
}

} // namespace lmshao::lmnet
