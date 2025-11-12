/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <ws2tcpip.h>

#include <cstdlib>

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("UdpClient");
}

UdpClientImpl::~UdpClientImpl()
{
    Close();

    // Give time for pending IOCP callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (taskQueue_) {
        taskQueue_->Stop();
    }
}

bool UdpClientImpl::Init()
{
    EnsureWsaInit();

    // Start TaskQueue first to accept callbacks
    if (taskQueue_->Start() != 0) {
        LMNET_LOGE("Failed to start TaskQueue");
        return false;
    }

    // Initialize IOCP manager
    auto &manager = IocpManager::GetInstance();
    if (!manager.Init()) {
        LMNET_LOGE("Failed to initialize IOCP manager");
        taskQueue_->Stop(); // Clean up on failure
        return false;
    }

    // Decide address family by remote/local IP
    use_ipv6_ =
        (remoteIp_.find(':') != std::string::npos) || (!localIp_.empty() && localIp_.find(':') != std::string::npos);
    int family = use_ipv6_ ? AF_INET6 : AF_INET;

    // Create UDP socket
    socket_ = WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }

    if (use_ipv6_) {
        // Enable dual-stack to allow IPv4-mapped replies when bound to ::
        DWORD v6only = 0;
        if (setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&v6only), sizeof(v6only)) ==
            SOCKET_ERROR) {
            LMNET_LOGW("setsockopt IPV6_V6ONLY failed: %d", WSAGetLastError());
        }
    }

    // Setup local binding
    if (!localIp_.empty() || localPort_ != 0) {
        if (use_ipv6_) {
            sockaddr_in6 l6{};
            l6.sin6_family = AF_INET6;
            l6.sin6_port = htons(localPort_);
            if (localIp_.empty()) {
                localIp_ = "::";
            }
            // Parse scope id for local IPv6 if provided (e.g., fe80::xxxx%12)
            unsigned long local_scope_id = 0;
            std::string local_ip_no_scope = localIp_;
            auto l_percent = local_ip_no_scope.find('%');
            if (l_percent != std::string::npos) {
                std::string scope = local_ip_no_scope.substr(l_percent + 1);
                local_ip_no_scope = local_ip_no_scope.substr(0, l_percent);
                if (!scope.empty()) {
                    char *endptr = nullptr;
                    unsigned long idx = std::strtoul(scope.c_str(), &endptr, 10);
                    if (endptr && *endptr == '\0') {
                        local_scope_id = idx;
                    } else {
                        LMNET_LOGW(
                            "IPv6 local scope '%s' is non-numeric; interface name scopes are not supported on Windows",
                            scope.c_str());
                    }
                }
            }
            if (inet_pton(AF_INET6, local_ip_no_scope.c_str(), &l6.sin6_addr) != 1) {
                LMNET_LOGE("inet_pton local(AF_INET6) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            l6.sin6_scope_id = local_scope_id; // Set scope for link-local bind
            if (bind(socket_, reinterpret_cast<sockaddr *>(&l6), sizeof(l6)) != 0) {
                LMNET_LOGE("bind(AF_INET6) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            memcpy(&localAddr_, &l6, sizeof(l6));
            localAddrLen_ = sizeof(l6);
        } else {
            sockaddr_in l4{};
            l4.sin_family = AF_INET;
            l4.sin_port = htons(localPort_);
            if (localIp_.empty()) {
                localIp_ = "0.0.0.0";
            }
            if (inet_pton(AF_INET, localIp_.c_str(), &l4.sin_addr) != 1) {
                LMNET_LOGE("inet_pton local(AF_INET) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            if (bind(socket_, reinterpret_cast<sockaddr *>(&l4), sizeof(l4)) != 0) {
                LMNET_LOGE("bind(AF_INET) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            memcpy(&localAddr_, &l4, sizeof(l4));
            localAddrLen_ = sizeof(l4);
        }
    } else {
        // For UDP client, bind to receive replies
        if (use_ipv6_) {
            sockaddr_in6 l6{};
            l6.sin6_family = AF_INET6;
            l6.sin6_addr = in6addr_any;
            l6.sin6_port = 0;
            if (bind(socket_, reinterpret_cast<sockaddr *>(&l6), sizeof(l6)) != 0) {
                LMNET_LOGE("Auto-bind(AF_INET6) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            memcpy(&localAddr_, &l6, sizeof(l6));
            localAddrLen_ = sizeof(l6);
        } else {
            sockaddr_in l4{};
            l4.sin_family = AF_INET;
            l4.sin_addr.s_addr = htonl(INADDR_ANY);
            l4.sin_port = 0;
            if (bind(socket_, reinterpret_cast<sockaddr *>(&l4), sizeof(l4)) != 0) {
                LMNET_LOGE("Auto-bind(AF_INET) failed: %d", WSAGetLastError());
                Close();
                return false;
            }
            memcpy(&localAddr_, &l4, sizeof(l4));
            localAddrLen_ = sizeof(l4);
        }
    }

    // Associate socket with IOCP
    if (!CreateIoCompletionPort((HANDLE)socket_, manager.GetIocpHandle(), (ULONG_PTR)socket_, 0)) {
        LMNET_LOGE("Failed to associate UDP client socket with IOCP: %lu", GetLastError());
        Close();
        return false;
    }

    // Initialize PacketOrderer for ordered delivery (IPv4/IPv6)
    packet_orderer_ = std::make_unique<PacketOrderer>(
        [this](std::shared_ptr<DataBuffer> buffer, const sockaddr_storage &addr, int addrLen) {
            // Ordered delivery to TaskQueue
            if (taskQueue_ && isRunning_.load()) {
                auto task = std::make_shared<TaskHandler<void>>(
                    [this, buffer, addr, addrLen]() { this->DeliverOrdered(buffer, addr, addrLen); });
                taskQueue_->EnqueueTask(task);
            }
        },
        64,                              // Max 64 out-of-order packets
        std::chrono::milliseconds(100)); // 100ms timeout

    // Setup remote address
    if (use_ipv6_) {
        sockaddr_in6 r6{};
        r6.sin6_family = AF_INET6;
        r6.sin6_port = htons(remotePort_);
        // Support link-local scope id: allow input like "fe80::xxxx%12"
        unsigned long scope_id = 0;
        std::string ip_no_scope = remoteIp_;
        auto percent_pos = ip_no_scope.find('%');
        if (percent_pos != std::string::npos) {
            std::string scope = ip_no_scope.substr(percent_pos + 1);
            ip_no_scope = ip_no_scope.substr(0, percent_pos);
            if (!scope.empty()) {
                char *endptr = nullptr;
                unsigned long idx = std::strtoul(scope.c_str(), &endptr, 10);
                if (endptr && *endptr == '\0') {
                    scope_id = idx;
                } else {
                    LMNET_LOGW("IPv6 scope '%s' is non-numeric; interface name scopes are not supported on Windows",
                               scope.c_str());
                }
            }
        }
        if (inet_pton(AF_INET6, ip_no_scope.c_str(), &r6.sin6_addr) != 1) {
            LMNET_LOGE("inet_pton remote(AF_INET6) failed: %d", WSAGetLastError());
            Close();
            return false;
        }
        r6.sin6_scope_id = scope_id; // Required for link-local addresses
        memcpy(&remoteAddr_, &r6, sizeof(r6));
        remoteAddrLen_ = sizeof(r6);
    } else {
        sockaddr_in r4{};
        r4.sin_family = AF_INET;
        r4.sin_port = htons(remotePort_);
        if (inet_pton(AF_INET, remoteIp_.c_str(), &r4.sin_addr) != 1) {
            LMNET_LOGE("inet_pton remote(AF_INET) failed: %d", WSAGetLastError());
            Close();
            return false;
        }
        memcpy(&remoteAddr_, &r4, sizeof(r4));
        remoteAddrLen_ = sizeof(r4);
    }

    isRunning_.store(true);
    StartReceiving();

    LMNET_LOGD("UDP client initialized: %s:%u (ordered delivery enabled)", remoteIp_.c_str(), remotePort_);
    return true;
}

void UdpClientImpl::SetListener(std::shared_ptr<IClientListener> listener)
{
    listener_ = listener;
}

bool UdpClientImpl::EnableBroadcast()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized, call Init() first");
        return false;
    }

    BOOL broadcast = TRUE;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, (const char *)&broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
        LMNET_LOGE("Failed to enable broadcast: %d", WSAGetLastError());
        return false;
    }

    LMNET_LOGD("Broadcast enabled successfully");
    return true;
}

void UdpClientImpl::StartReceiving()
{
    // Start multiple concurrent receive operations for better performance
    for (int i = 0; i < CONCURRENT_RECEIVES; ++i) {
        SubmitReceive();
    }
}

void UdpClientImpl::SubmitReceive()
{
    if (!isRunning_.load()) {
        return;
    }

    auto buffer = DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    // Assign sequence number at submission time to preserve order
    uint64_t seq = receive_seq_counter_.fetch_add(1, std::memory_order_relaxed);

    bool success = manager.SubmitRecvFromRequest(
        socket_, buffer,
        [self, buffer, seq](SOCKET socket, std::shared_ptr<DataBuffer> buf, DWORD bytesOrError,
                            const sockaddr_storage &fromAddr, int fromLen) {
            // This callback may be invoked from multiple IOCP worker threads in non-deterministic order
            // Submit to PacketOrderer for reordering
            if (self->isRunning_.load() && self->packet_orderer_) {
                if (bytesOrError > 0 && bytesOrError <= 65536) {
                    // Valid data packet - submit for ordering
                    buf->SetSize(bytesOrError);
                    self->packet_orderer_->SubmitPacket(seq, buf, fromAddr, fromLen);
                } else if (bytesOrError > 65536) {
                    // Error code
                    LMNET_LOGE("UDP receive error: %lu", bytesOrError);
                    if (self->taskQueue_) {
                        auto task = std::make_shared<TaskHandler<void>>([self, bytesOrError]() {
                            self->HandleClose(true, "Receive error: " + std::to_string(bytesOrError));
                        });
                        self->taskQueue_->EnqueueTask(task);
                    }
                }
            }

            // Continue receiving
            self->SubmitReceive();
        });

    if (!success) {
        LMNET_LOGE("Failed to submit UDP receive request");
        if (taskQueue_) {
            auto task = std::make_shared<TaskHandler<void>>(
                [self]() { self->HandleClose(true, "Failed to submit receive request"); });
            taskQueue_->EnqueueTask(task);
        } else {
            HandleClose(true, "Failed to submit receive request");
        }
    }
}

void UdpClientImpl::DeliverOrdered(std::shared_ptr<DataBuffer> buffer, const sockaddr_storage &fromAddr, int fromLen)
{
    if (!isRunning_.load() || !buffer) {
        return;
    }

    // Notify listener (packets are now in order)
    if (auto listener = listener_.lock()) {
        listener->OnReceive(socket_, buffer);
    }
}

bool UdpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        return false;
    }

    auto buffer = DataBuffer::Create(len);
    buffer->Assign(data, len);
    return Send(buffer);
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        return false;
    }

    if (!isRunning_.load()) {
        LMNET_LOGE("UDP client not running");
        return false;
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitSendToRequest(socket_, data, reinterpret_cast<const sockaddr *>(&remoteAddr_),
                                               remoteAddrLen_, [self](SOCKET socket, DWORD bytesOrError) {
                                                   if (self->taskQueue_) {
                                                       auto task = std::make_shared<TaskHandler<void>>(
                                                           [self, bytesOrError]() { self->HandleSend(bytesOrError); });
                                                       self->taskQueue_->EnqueueTask(task);
                                                   } else {
                                                       self->HandleSend(bytesOrError);
                                                   }
                                               });

    if (!success) {
        LMNET_LOGE("Failed to submit UDP send request");
        return false;
    }

    return true;
}

void UdpClientImpl::HandleSend(DWORD bytesOrError)
{
    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("UDP send error: %lu", bytesOrError);
        HandleClose(true, "Send error: " + std::to_string(bytesOrError));
    }
    // For successful sends, we don't need to do anything special
}

void UdpClientImpl::Close()
{
    if (!isRunning_.load()) {
        return;
    }

    LMNET_LOGD("Closing UDP client");

    isRunning_.store(false);

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    // Note: Don't stop or reset taskQueue_ here - let the destructor handle it
    // This avoids race conditions with IOCP worker threads still processing completions

    LMNET_LOGD("UDP client closed");
}

void UdpClientImpl::HandleClose(bool isError, const std::string &reason)
{
    if (!isRunning_.load()) {
        return;
    }

    LMNET_LOGD("Handling UDP client close: error=%d, reason=%s", isError, reason.c_str());

    // Notify listener
    if (auto listener = listener_.lock()) {
        if (isError) {
            listener->OnError(socket_, reason);
        } else {
            listener->OnClose(socket_);
        }
    }
}

socket_t UdpClientImpl::GetSocketFd() const
{
    return socket_;
}

} // namespace lmshao::lmnet
