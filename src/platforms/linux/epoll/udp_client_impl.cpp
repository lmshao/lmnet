/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "event_reactor.h"
#include "internal_logger.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskHandler;

const int RECV_BUFFER_MAX_SIZE = 4096;

class UdpClientHandler : public EventHandler {
public:
    UdpClientHandler(socket_t fd, std::weak_ptr<UdpClientImpl> client) : fd_(fd), client_(client) {}

    void HandleRead(socket_t fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t fd) override {}

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("UDP client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("UDP client connection close on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
        }
    }

    socket_t GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    socket_t fd_;
    std::weak_ptr<UdpClientImpl> client_;
};

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("UdpClientCb");
}

UdpClientImpl::~UdpClientImpl()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool UdpClientImpl::Init()
{
    // Resolve remote address (numeric only to keep API stable)
    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(remotePort_));
    struct addrinfo *res = nullptr;
    int rv = getaddrinfo(remoteIp_.c_str(), port_str, &hints, &res);
    if (rv != 0 || res == nullptr) {
        LMNET_LOGE("getaddrinfo remote failed: %s", gai_strerror(rv));
        return false;
    }
    // Enforce single-stack: pick the first resolved address
    struct addrinfo *picked = res;
    int family = picked->ai_family;

    // Create one socket matching remote family
    socket_t s = socket(family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (s == INVALID_SOCKET) {
        LMNET_LOGE("create socket failed for family %d: %s", family, strerror(errno));
        freeaddrinfo(res);
        return false;
    }

    // Optional local bind using the same family
    if (!localIp_.empty() || localPort_ != 0) {
        if (family == AF_INET) {
            struct sockaddr_in local4 {};
            memset(&local4, 0, sizeof(local4));
            local4.sin_family = AF_INET;
            local4.sin_port = htons(localPort_);
            const char *bind_ip4 = localIp_.empty() ? "0.0.0.0" : localIp_.c_str();
            if (inet_pton(AF_INET, bind_ip4, &local4.sin_addr) == 1) {
                if (bind(s, (struct sockaddr *)&local4, sizeof(local4)) != 0) {
                    LMNET_LOGE("bind IPv4 error: %s", strerror(errno));
                }
            }
        } else if (family == AF_INET6) {
            struct sockaddr_in6 local6 {};
            memset(&local6, 0, sizeof(local6));
            local6.sin6_family = AF_INET6;
            local6.sin6_port = htons(localPort_);
            const char *bind_ip6 = localIp_.empty() ? "::" : localIp_.c_str();
            if (inet_pton(AF_INET6, bind_ip6, &local6.sin6_addr) == 1) {
                if (bind(s, (struct sockaddr *)&local6, sizeof(local6)) != 0) {
                    LMNET_LOGE("bind IPv6 error: %s", strerror(errno));
                }
            }
        }
    }

    // Cache remote address
    memcpy(&remote_addr_, picked->ai_addr, picked->ai_addrlen);
    remote_addr_len_ = picked->ai_addrlen;
    freeaddrinfo(res);

    // Start task queue
    taskQueue_->Start();

    // Register only one handler according to the chosen family
    auto self = shared_from_this();
    bool ok = true;
    if (family == AF_INET) {
        ipv4_socket_ = s;
        ipv4_handler_ = std::make_shared<UdpClientHandler>(ipv4_socket_, self);
        ok = EventReactor::GetInstance().RegisterHandler(ipv4_handler_);
    } else {
        ipv6_socket_ = s;
        ipv6_handler_ = std::make_shared<UdpClientHandler>(ipv6_socket_, self);
        ok = EventReactor::GetInstance().RegisterHandler(ipv6_handler_);
    }
    if (!ok) {
        LMNET_LOGE("Failed to register UDP client handler");
        if (family == AF_INET) {
            EventReactor::GetInstance().RemoveHandler(ipv4_socket_);
            close(ipv4_socket_);
            ipv4_socket_ = INVALID_SOCKET;
            ipv4_handler_.reset();
        } else {
            EventReactor::GetInstance().RemoveHandler(ipv6_socket_);
            close(ipv6_socket_);
            ipv6_socket_ = INVALID_SOCKET;
            ipv6_handler_.reset();
        }
        return false;
    }

    // Log single-stack initialization details
    if (family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&remote_addr_);
        char ipbuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UdpClientImpl initialized single-stack IPv4 remote %s:%u", ipbuf,
                   static_cast<unsigned>(ntohs(sin->sin_port)));
    } else {
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(&remote_addr_);
        char ipbuf[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &sin6->sin6_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UdpClientImpl initialized single-stack IPv6 remote [%s]:%u", ipbuf,
                   static_cast<unsigned>(ntohs(sin6->sin6_port)));
    }
    return true;
}

bool UdpClientImpl::EnableBroadcast()
{
    // Broadcast is IPv4-only; enable if IPv4 socket exists
    if (ipv4_socket_ == INVALID_SOCKET) {
        LMNET_LOGW("Broadcast not available: IPv4 socket not initialized");
        return false;
    }
    int broadcast = 1;
    if (setsockopt(ipv4_socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LMNET_LOGE("Failed to enable broadcast: %s", strerror(errno));
        return false;
    }
    LMNET_LOGD("Broadcast enabled successfully on IPv4 socket");
    return true;
}

void UdpClientImpl::Close()
{
    if (ipv4_socket_ != INVALID_SOCKET && ipv4_handler_) {
        EventReactor::GetInstance().RemoveHandler(ipv4_socket_);
        close(ipv4_socket_);
        ipv4_socket_ = INVALID_SOCKET;
        ipv4_handler_.reset();
    }
    if (ipv6_socket_ != INVALID_SOCKET && ipv6_handler_) {
        EventReactor::GetInstance().RemoveHandler(ipv6_socket_);
        close(ipv6_socket_);
        ipv6_socket_ = INVALID_SOCKET;
        ipv6_handler_.reset();
    }
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if ((!data) || len == 0) {
        LMNET_LOGE("invalid send parameters: data=%p, len=%zu", data, len);
        return false;
    }
    if (remote_addr_len_ == 0) {
        LMNET_LOGE("remote address not resolved; call Init() first");
        return false;
    }

    int send_fd = INVALID_SOCKET;
    if (remote_addr_.ss_family == AF_INET) {
        send_fd = ipv4_socket_;
    } else if (remote_addr_.ss_family == AF_INET6) {
        send_fd = ipv6_socket_;
    }
    if (send_fd == INVALID_SOCKET) {
        LMNET_LOGE("no valid socket for remote family %d", remote_addr_.ss_family);
        return false;
    }

    ssize_t nbytes = sendto(send_fd, data, len, 0, (struct sockaddr *)&remote_addr_, remote_addr_len_);
    if (nbytes == -1) {
        LMNET_LOGE("sendto error: %s", strerror(errno));
        return false;
    }
    if (static_cast<size_t>(nbytes) != len) {
        LMNET_LOGW("partial send: %zd/%zu", nbytes, len);
        return false;
    }
    return true;
}

bool UdpClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("invalid send parameters: empty string");
        return false;
    }
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data) {
        return false;
    }
    return Send(data->Data(), data->Size());
}

void UdpClientImpl::HandleReceive(socket_t fd)
{
    LMNET_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);
                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>([listenerWeak, dataBuffer, fd]() {
                    auto listener = listenerWeak.lock();
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
            LMNET_LOGW("Disconnect fd[%d]", fd);
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // Usually same value, but check both for portability
                // Normal case: no data available to read, return directly
                break;
            }

            std::string info = strerror(errno);
            LMNET_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void UdpClientImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing UDP client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
               isError ? "true" : "false");

    if (fd == ipv4_socket_) {
        EventReactor::GetInstance().RemoveHandler(fd);
        close(fd);
        ipv4_socket_ = INVALID_SOCKET;
        ipv4_handler_.reset();
    } else if (fd == ipv6_socket_) {
        EventReactor::GetInstance().RemoveHandler(fd);
        close(fd);
        ipv6_socket_ = INVALID_SOCKET;
        ipv6_handler_.reset();
    } else {
        LMNET_LOGD("Unknown fd[%d] for UDP client", fd);
        return;
    }

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, reason, isError, fd]() {
            auto listener = listenerWeak.lock();
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
} // namespace lmshao::lmnet