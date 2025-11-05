/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_server_impl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <utility>

#include "event_reactor.h"
#include "internal_logger.h"
#include "udp_session_impl.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

// UDP Server Handler
class UdpServerHandler : public EventHandler {
public:
    UdpServerHandler(socket_t fd, std::weak_ptr<UdpServerImpl> server) : fd_(fd), server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t fd) override {}

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("UDP server connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            LMNET_LOGE("UDP server socket error occurred");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("UDP server connection close on fd: %d", fd);
        if (auto server = server_.lock()) {
            LMNET_LOGD("UDP server socket closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    socket_t fd_;
    std::weak_ptr<UdpServerImpl> server_;
};

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port)
    : ip_(std::move(ip)), port_(port), taskQueue_(std::make_unique<TaskQueue>("UdpServer")),
      readBuffer_(std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE))
{
}

UdpServerImpl::~UdpServerImpl()
{
    Stop();
}

bool UdpServerImpl::Init()
{
    // Decide if we want dual binding (both IPv4 and IPv6)
    auto is_wildcard = [&](const std::string &s) { return s.empty() || s == "*" || s == "0.0.0.0" || s == "::"; };

    bool want_dual = is_wildcard(ip_);

    struct addrinfo hints {};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = want_dual ? AI_PASSIVE : AI_NUMERICHOST; // keep numeric-only behavior for specific IP

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port_));

    struct addrinfo *result = nullptr;
    int rv = getaddrinfo(want_dual ? nullptr : ip_.c_str(), port_str, &hints, &result);
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
            int s = ::socket(AF_INET, SOCK_DGRAM, 0);
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

            ipv4_socket_ = s;
            server_addr4_ = *reinterpret_cast<sockaddr_in *>(ai->ai_addr);
            bound_v4 = true;
        } else if (ai->ai_family == AF_INET6 && !bound_v6) {
            int s = ::socket(AF_INET6, SOCK_DGRAM, 0);
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

            // Ensure no v4-mapped takeover when dual-binding
            if (want_dual) {
                int v6only = 1;
                if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only)) < 0) {
                    LMNET_LOGE("setsockopt IPV6_V6ONLY failed: %s", strerror(errno));
                    // Continue; not fatal in some envs
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

            ipv6_socket_ = s;
            server_addr6_ = *reinterpret_cast<sockaddr_in6 *>(ai->ai_addr);
            bound_v6 = true;
        }
    }

    freeaddrinfo(result);

    if (!bound_v4 && !bound_v6) {
        LMNET_LOGE("UDP server init failed: no sockets bound for %s:%u", ip_.c_str(), static_cast<unsigned>(port_));
        return false;
    }

    if (bound_v4 && bound_v6) {
        LMNET_LOGD("UDP server initialized dual-stack on port %u", static_cast<unsigned>(port_));
    } else if (bound_v4) {
        char ipbuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &server_addr4_.sin_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UDP server initialized IPv4 on %s:%u", ipbuf, static_cast<unsigned>(port_));
    } else if (bound_v6) {
        char ipbuf[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &server_addr6_.sin6_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UDP server initialized IPv6 on [%s]:%u", ipbuf, static_cast<unsigned>(port_));
    }

    return true;
}

bool UdpServerImpl::Start()
{
    if (ipv4_socket_ == INVALID_SOCKET && ipv6_socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket is not initialized");
        return false;
    }

    auto self = shared_from_this();

    bool ok = true;
    if (ipv4_socket_ != INVALID_SOCKET) {
        ipv4_handler_ = std::make_shared<UdpServerHandler>(ipv4_socket_, self);
        ok = ok && EventReactor::GetInstance().RegisterHandler(ipv4_handler_);
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        ipv6_handler_ = std::make_shared<UdpServerHandler>(ipv6_socket_, self);
        ok = ok && EventReactor::GetInstance().RegisterHandler(ipv6_handler_);
    }
    if (!ok) {
        LMNET_LOGE("Failed to add server handlers to event reactor");
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

    if (taskQueue_->Start() != 0) {
        LMNET_LOGE("Failed to start task queue");
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

    LMNET_LOGD("UDP server started successfully on port %u", static_cast<unsigned>(port_));
    return true;
}

bool UdpServerImpl::Stop()
{
    LMNET_LOGD("Stopping UDP server");

    // Stop task queue first
    if (taskQueue_) {
        taskQueue_->Stop();
    }

    // Remove from event loop
    if (ipv4_handler_) {
        EventReactor::GetInstance().RemoveHandler(ipv4_socket_);
        ipv4_handler_.reset();
    }
    if (ipv6_handler_) {
        EventReactor::GetInstance().RemoveHandler(ipv6_socket_);
        ipv6_handler_.reset();
    }

    // Close socket
    if (ipv4_socket_ != INVALID_SOCKET) {
        close(ipv4_socket_);
        ipv4_socket_ = INVALID_SOCKET;
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        close(ipv6_socket_);
        ipv6_socket_ = INVALID_SOCKET;
    }

    LMNET_LOGD("UDP server stopped");
    return true;
}

void UdpServerImpl::HandleReceive(socket_t fd)
{
    struct sockaddr_storage clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    readBuffer_->Clear();
    readBuffer_->SetCapacity(RECV_BUFFER_MAX_SIZE);

    ssize_t bytesRead = recvfrom(fd, readBuffer_->Data(), readBuffer_->Capacity(), 0,
                                 reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen);

    if (bytesRead > 0) {
        readBuffer_->SetSize(bytesRead);
        std::string remoteIp;
        uint16_t clientPort = 0;

        if (clientAddr.ss_family == AF_INET) {
            auto *sin = reinterpret_cast<struct sockaddr_in *>(&clientAddr);
            char ipbuf[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
            remoteIp = ipbuf;
            clientPort = ntohs(sin->sin_port);
        } else if (clientAddr.ss_family == AF_INET6) {
            auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(&clientAddr);
            char ipbuf[INET6_ADDRSTRLEN]{};
            inet_ntop(AF_INET6, &sin6->sin6_addr, ipbuf, sizeof(ipbuf));
            remoteIp = ipbuf;
            clientPort = ntohs(sin6->sin6_port);
        }

        auto session = std::make_shared<UdpSessionImpl>(fd, remoteIp, clientPort, shared_from_this());

        if (auto listener = listener_.lock()) {
            auto dataBuffer = std::make_shared<DataBuffer>(*readBuffer_);
            listener->OnReceive(session, dataBuffer);
        }
    } else if (bytesRead == 0) {
        LMNET_LOGD("UDP peer closed connection");
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LMNET_LOGE("recvfrom failed: %s", strerror(errno));
        }
    }
}

bool UdpServerImpl::Send(std::string ip, uint16_t port, const void *data, size_t len)
{
    if (ipv4_socket_ == INVALID_SOCKET && ipv6_socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket is not initialized");
        return false;
    }

    struct addrinfo hints {};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST; // keep numeric-only behavior

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port));

    struct addrinfo *res = nullptr;
    int rv = getaddrinfo(ip.c_str(), port_str, &hints, &res);
    if (rv != 0 || res == nullptr) {
        LMNET_LOGE("getaddrinfo failed for %s:%u: %s", ip.c_str(), static_cast<unsigned>(port), gai_strerror(rv));
        return false;
    }

    bool ok = false;
    for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
        int send_fd = INVALID_SOCKET;
        if (ai->ai_family == AF_INET && ipv4_socket_ != INVALID_SOCKET) {
            send_fd = ipv4_socket_;
        } else if (ai->ai_family == AF_INET6 && ipv6_socket_ != INVALID_SOCKET) {
            send_fd = ipv6_socket_;
        } else {
            continue;
        }

        ssize_t bytesSent = sendto(send_fd, data, len, 0, ai->ai_addr, ai->ai_addrlen);
        if (bytesSent < 0) {
            LMNET_LOGE("sendto failed: %s", strerror(errno));
            continue;
        }
        if (static_cast<size_t>(bytesSent) != len) {
            LMNET_LOGW("Partial send: sent %zd bytes out of %zu", bytesSent, len);
            continue;
        }
        ok = true;
        break;
    }

    freeaddrinfo(res);
    return ok;
}

bool UdpServerImpl::Send(std::string ip, uint16_t port, const std::string &str)
{
    return Send(std::move(ip), port, str.data(), str.size());
}

bool UdpServerImpl::Send(std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    if (!data) {
        LMNET_LOGE("DataBuffer is null");
        return false;
    }
    return Send(std::move(ip), port, data->Data(), data->Size());
}

} // namespace lmshao::lmnet
