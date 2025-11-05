#include "udp_server_impl.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "io_uring_session_impl.h"

namespace lmshao::lmnet {

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port) : ip_(std::move(ip)), port_(port) {}

UdpServerImpl::~UdpServerImpl()
{
    Stop();
}

bool UdpServerImpl::Init()
{
    // Resolve local address for dual-stack UDP
    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[16] = {0};
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(port_));

    struct addrinfo *res = nullptr;
    // Treat wildcard inputs as dual-stack wildcard
    bool want_dual = ip_.empty() || ip_ == "0.0.0.0" || ip_ == "::" || ip_ == "*";
    int gai = getaddrinfo(want_dual ? nullptr : ip_.c_str(), port_str, &hints, &res);
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

        if (ai->ai_family == AF_INET) {
            ipv4_socket_ = s;
        } else if (ai->ai_family == AF_INET6) {
            ipv6_socket_ = s;
        }
        any_success = true;
    }

    freeaddrinfo(res);

    if (!any_success) {
        LMNET_LOGE("No UDP sockets created for %s:%u", ip_.c_str(), port_);
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
    return true;
}

bool UdpServerImpl::Start()
{
    if (!isRunning_)
        return false;
    if (ipv4_socket_ != INVALID_SOCKET) {
        StartReceiveOnSocket(ipv4_socket_);
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        StartReceiveOnSocket(ipv6_socket_);
    }
    LMNET_LOGI("UDP server started on %s:%d", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }
    if (ipv4_socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(ipv4_socket_, nullptr);
        ipv4_socket_ = INVALID_SOCKET;
    }
    if (ipv6_socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(ipv6_socket_, nullptr);
        ipv6_socket_ = INVALID_SOCKET;
    }
    LMNET_LOGI("UDP server stopped.");
    return true;
}

void UdpServerImpl::StartReceiveOnSocket(socket_t fd)
{
    if (!isRunning_)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitRecvFromRequest(
        fd, buffer,
        [self](int sockfd, std::shared_ptr<DataBuffer> buf, int bytes_read, const sockaddr_storage &from,
               socklen_t len) { self->HandleReceive(buf, bytes_read, from, len); });
}

void UdpServerImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_storage &from_addr,
                                  socklen_t addrlen)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            char client_ip[INET6_ADDRSTRLEN] = {0};
            uint16_t client_port = 0;
            socket_t server_fd = INVALID_SOCKET;
            if (from_addr.ss_family == AF_INET) {
                const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(&from_addr);
                inet_ntop(AF_INET, &in->sin_addr, client_ip, sizeof(client_ip));
                client_port = ntohs(in->sin_port);
                server_fd = ipv4_socket_;
            } else if (from_addr.ss_family == AF_INET6) {
                const sockaddr_in6 *in6 = reinterpret_cast<const sockaddr_in6 *>(&from_addr);
                inet_ntop(AF_INET6, &in6->sin6_addr, client_ip, sizeof(client_ip));
                client_port = ntohs(in6->sin6_port);
                server_fd = ipv6_socket_;
            }

            auto session = std::make_shared<IoUringSessionImpl>(server_fd, std::string(client_ip), client_port,
                                                                from_addr, addrlen, true);

            // For UDP, call the listener with session
            listener->OnReceive(session, buffer);
        }
    } else if (bytes_read < 0) {
        if (isRunning_) {
            LMNET_LOGE("RecvFrom failed: %s", strerror(-bytes_read));
        }
    }

    // Always restart receiving unless the server is stopped.
    if (isRunning_) {
        if (ipv4_socket_ != INVALID_SOCKET) {
            StartReceiveOnSocket(ipv4_socket_);
        }
        if (ipv6_socket_ != INVALID_SOCKET) {
            StartReceiveOnSocket(ipv6_socket_);
        }
    }
}

} // namespace lmshao::lmnet
