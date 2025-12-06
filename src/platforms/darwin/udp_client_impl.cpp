/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "event_reactor.h"
#include "internal_logger.h"
#include "socket_utils.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;
using namespace darwin;

namespace {
constexpr int RECV_BUFFER_MAX_SIZE = 4096;
}

class UdpClientHandler : public EventHandler {
public:
    UdpClientHandler(socket_t fd, std::weak_ptr<UdpClientImpl> client) : fd_(fd), client_(std::move(client)) {}

    void HandleRead(socket_t fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t) override {}

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("UDP client error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("UDP client closed on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
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
    std::weak_ptr<UdpClientImpl> client_;
};

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
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
    // Resolve remote address (numeric only)
    struct addrinfo hints{};
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST;

    char port_str[16];
    std::snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(remotePort_));
    struct addrinfo *res = nullptr;
    int rv = getaddrinfo(remoteIp_.c_str(), port_str, &hints, &res);
    if (rv != 0 || res == nullptr) {
        LMNET_LOGE("getaddrinfo remote failed: %s", gai_strerror(rv));
        return false;
    }

    struct addrinfo *picked = res;
    int family = picked->ai_family;

    // Create one socket matching remote family
    socket_ = ::socket(family, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket creation failed: %s", strerror(errno));
        freeaddrinfo(res);
        return false;
    }

    if (!SetNonBlocking(socket_) || !SetCloseOnExec(socket_)) {
        LMNET_LOGE("Failed to configure UDP client socket: %s", strerror(errno));
        Close();
        freeaddrinfo(res);
        return false;
    }

    DisableSigpipe(socket_);

    // Optional local bind using the same family
    if (!localIp_.empty() || localPort_ != 0) {
        if (family == AF_INET) {
            struct sockaddr_in local4{};
            std::memset(&local4, 0, sizeof(local4));
            local4.sin_family = AF_INET;
            local4.sin_port = htons(localPort_);
            const char *bind_ip4 = localIp_.empty() ? "0.0.0.0" : localIp_.c_str();
            if (inet_pton(AF_INET, bind_ip4, &local4.sin_addr) == 1) {
                if (bind(socket_, reinterpret_cast<struct sockaddr *>(&local4), sizeof(local4)) != 0) {
                    LMNET_LOGE("bind IPv4 error: %s", strerror(errno));
                }
            }
        } else if (family == AF_INET6) {
            struct sockaddr_in6 local6{};
            std::memset(&local6, 0, sizeof(local6));
            local6.sin6_family = AF_INET6;
            local6.sin6_port = htons(localPort_);
            const char *bind_ip6 = localIp_.empty() ? "::" : localIp_.c_str();
            if (inet_pton(AF_INET6, bind_ip6, &local6.sin6_addr) == 1) {
                if (bind(socket_, reinterpret_cast<struct sockaddr *>(&local6), sizeof(local6)) != 0) {
                    LMNET_LOGE("bind IPv6 error: %s", strerror(errno));
                }
            }
        }
    }

    // Cache remote address and length
    std::memcpy(&remote_addr_, picked->ai_addr, picked->ai_addrlen);
    remote_addr_len_ = picked->ai_addrlen;
    freeaddrinfo(res);

    if (taskQueue_->Start() != 0) {
        LMNET_LOGE("Failed to start UDP client task queue");
        Close();
        return false;
    }

    clientHandler_ = std::make_shared<UdpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(clientHandler_)) {
        LMNET_LOGE("Failed to register UDP client handler");
        Close();
        return false;
    }

    if (family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&remote_addr_);
        char ipbuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UDP client initialized IPv4 remote %s:%u", ipbuf, static_cast<unsigned>(ntohs(sin->sin_port)));
    } else {
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(&remote_addr_);
        char ipbuf[INET6_ADDRSTRLEN]{};
        inet_ntop(AF_INET6, &sin6->sin6_addr, ipbuf, sizeof(ipbuf));
        LMNET_LOGD("UDP client initialized IPv6 remote [%s]:%u", ipbuf, static_cast<unsigned>(ntohs(sin6->sin6_port)));
    }
    return true;
}

bool UdpClientImpl::EnableBroadcast()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized");
        return false;
    }

    int broadcast = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LMNET_LOGE("Failed to enable broadcast: %s", strerror(errno));
        return false;
    }

    return true;
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized");
        return false;
    }
    if (remote_addr_len_ == 0) {
        LMNET_LOGE("Remote address not set; call Init() first");
        return false;
    }

    if (!data || len == 0) {
        LMNET_LOGE("Invalid send parameters: data=%p len=%zu", data, len);
        return false;
    }

    ssize_t nbytes =
        sendto(socket_, data, len, 0, reinterpret_cast<struct sockaddr *>(&remote_addr_), remote_addr_len_);
    if (nbytes < 0) {
        LMNET_LOGE("sendto failed: %s", strerror(errno));
        return false;
    }
    if (static_cast<size_t>(nbytes) != len) {
        LMNET_LOGW("Partial UDP send: %zd of %zu bytes", nbytes, len);
    }
    return true;
}

bool UdpClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("Cannot send empty string");
        return false;
    }
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data) {
        LMNET_LOGE("Data buffer is null");
        return false;
    }
    return Send(data->Data(), data->Size());
}

void UdpClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET) {
        if (clientHandler_) {
            EventReactor::GetInstance().RemoveHandler(socket_);
            clientHandler_.reset();
        }
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void UdpClientImpl::HandleReceive(socket_t fd)
{
    if (!readBuffer_) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);
        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(static_cast<size_t>(nbytes));
                dataBuffer->Assign(readBuffer_->Data(), static_cast<size_t>(nbytes));
                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>([listenerWeak, dataBuffer, fd]() {
                    if (auto listener = listenerWeak.lock()) {
                        listener->OnReceive(fd, dataBuffer);
                    }
                });
                if (taskQueue_) {
                    taskQueue_->EnqueueTask(task);
                }
            }
            continue;
        }

        if (nbytes == 0) {
            HandleConnectionClose(fd, false, "Peer closed");
            break;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        std::string reason = strerror(errno);
        HandleConnectionClose(fd, true, reason);
        break;
    }
}

void UdpClientImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing UDP client fd %d reason %s isError %s", fd, reason.c_str(), isError ? "true" : "false");

    if (socket_ != fd) {
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);
    close(fd);
    socket_ = INVALID_SOCKET;
    clientHandler_.reset();

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, reason, isError, fd]() {
            if (auto listener = listenerWeak.lock()) {
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
