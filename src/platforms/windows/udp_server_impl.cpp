/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_server_impl.h"

#include <mswsock.h>
#include <ws2tcpip.h>

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"
#include "udp_session_impl.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

UdpServerImpl::UdpServerImpl(std::string listenIp, uint16_t listenPort) : ip_(std::move(listenIp)), port_(listenPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("UdpServer");
}

UdpServerImpl::UdpServerImpl(uint16_t listenPort) : UdpServerImpl("0.0.0.0", listenPort) {}

UdpServerImpl::~UdpServerImpl()
{
    Stop();

    // Give time for pending IOCP callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (taskQueue_) {
        taskQueue_->Stop();
    }
}

bool UdpServerImpl::Init()
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

    // Decide address family by input IP
    use_ipv6_ = (ip_.find(':') != std::string::npos);
    int family = use_ipv6_ ? AF_INET6 : AF_INET;

    // Default wildcard by family
    if (ip_.empty()) {
        ip_ = use_ipv6_ ? std::string("::") : std::string("0.0.0.0");
    }

    // Create UDP socket
    socket_ = WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }

    if (use_ipv6_) {
        // Enable dual-stack (allow IPv4-mapped connections) when appropriate
        DWORD v6only = 0; // 0 = dual-stack
        if (setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char *>(&v6only), sizeof(v6only)) ==
            SOCKET_ERROR) {
            LMNET_LOGW("setsockopt IPV6_V6ONLY failed: %d", WSAGetLastError());
        }

        // Setup IPv6 listen address
        ZeroMemory(&listenAddr6_, sizeof(listenAddr6_));
        listenAddr6_.sin6_family = AF_INET6;
        listenAddr6_.sin6_port = htons(port_);
        if (inet_pton(AF_INET6, ip_.c_str(), &listenAddr6_.sin6_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET6) failed for %s", ip_.c_str());
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        // Bind IPv6
        if (bind(socket_, reinterpret_cast<sockaddr *>(&listenAddr6_), sizeof(listenAddr6_)) != 0) {
            LMNET_LOGE("bind(AF_INET6) failed: %d", WSAGetLastError());
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    } else {
        // Setup IPv4 listen address
        ZeroMemory(&listenAddr_, sizeof(listenAddr_));
        listenAddr_.sin_family = AF_INET;
        listenAddr_.sin_port = htons(port_);
        if (inet_pton(AF_INET, ip_.c_str(), &listenAddr_.sin_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET) failed for %s", ip_.c_str());
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        // Bind IPv4
        if (bind(socket_, reinterpret_cast<sockaddr *>(&listenAddr_), sizeof(listenAddr_)) != 0) {
            LMNET_LOGE("bind(AF_INET) failed: %d", WSAGetLastError());
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    }

    // Associate socket with IOCP
    auto &iocpManager = IocpManager::GetInstance();
    if (!CreateIoCompletionPort((HANDLE)socket_, iocpManager.GetIocpHandle(), (ULONG_PTR)socket_, 0)) {
        LMNET_LOGE("Failed to associate UDP server socket with IOCP: %lu", GetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
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

    LMNET_LOGD("UDP server initialized on %s:%u (ordered delivery enabled)", ip_.c_str(), port_);
    return true;
}

void UdpServerImpl::SetListener(std::shared_ptr<IServerListener> listener)
{
    listener_ = listener;
}

bool UdpServerImpl::Start()
{
    if (isRunning_.load()) {
        LMNET_LOGW("UDP server already running");
        return true;
    }

    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("UDP server not initialized");
        return false;
    }

    isRunning_.store(true);
    StartReceiving();

    LMNET_LOGI("UDP server started on %s:%u", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Stop()
{
    if (!isRunning_.load()) {
        return true;
    }

    LMNET_LOGI("Stopping UDP server");

    isRunning_.store(false);

    // Close socket (this will cause pending I/O to complete with errors)
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    // Note: Don't stop or reset taskQueue_ here - let the destructor handle it
    // This avoids race conditions with IOCP worker threads still processing completions

    LMNET_LOGI("UDP server stopped");
    return true;
}

void UdpServerImpl::StartReceiving()
{
    // Start multiple concurrent receive operations for better performance
    for (int i = 0; i < CONCURRENT_RECEIVES; ++i) {
        SubmitReceive();
    }
}

void UdpServerImpl::SubmitReceive()
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
            // This callback may be invoked from multiple IOCP worker threads
            // in non-deterministic order Submit to PacketOrderer for reordering
            if (self->isRunning_.load() && self->packet_orderer_) {
                if (bytesOrError > 0 && bytesOrError <= 65536) {
                    // Valid data packet - submit for ordering
                    buf->SetSize(bytesOrError);
                    self->packet_orderer_->SubmitPacket(seq, buf, fromAddr, fromLen);
                } else if (bytesOrError > 65536) {
                    // Error code
                    LMNET_LOGE("UDP receive error: %lu", bytesOrError);
                }
            }

            // Continue receiving
            self->SubmitReceive();
        });

    if (!success) {
        LMNET_LOGE("Failed to submit UDP receive request");
        // Retry after a short delay in case of temporary issues
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (isRunning_.load()) {
            if (taskQueue_) {
                auto task = std::make_shared<TaskHandler<void>>([self]() { self->SubmitReceive(); });
                taskQueue_->EnqueueTask(task);
            } else {
                SubmitReceive();
            }
        }
    }
}

void UdpServerImpl::DeliverOrdered(std::shared_ptr<DataBuffer> buffer, const sockaddr_storage &fromAddr, int fromLen)
{
    if (!isRunning_.load() || !buffer) {
        return;
    }

    // Extract client info for IPv4/IPv6
    std::string host;
    uint16_t port = 0;

    if (fromAddr.ss_family == AF_INET) {
        const sockaddr_in *in4 = reinterpret_cast<const sockaddr_in *>(&fromAddr);
        char addrStr[INET_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET, const_cast<in_addr *>(&in4->sin_addr), addrStr, INET_ADDRSTRLEN) != nullptr) {
            host = addrStr;
        }
        port = ntohs(in4->sin_port);
    } else if (fromAddr.ss_family == AF_INET6) {
        const sockaddr_in6 *in6 = reinterpret_cast<const sockaddr_in6 *>(&fromAddr);
        char addrStr[INET6_ADDRSTRLEN]{};
        if (inet_ntop(AF_INET6, const_cast<in6_addr *>(&in6->sin6_addr), addrStr, INET6_ADDRSTRLEN) != nullptr) {
            host = addrStr;
        }
        port = ntohs(in6->sin6_port);
    }

    auto session = std::make_shared<UdpSessionImpl>((socket_t)socket_, host, port, shared_from_this());

    // Notify listener (packets are now in order)
    if (auto listener = listener_.lock()) {
        listener->OnReceive(session, buffer);
    }
}

bool UdpServerImpl::Send(std::string host, uint16_t port, const void *data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }

    auto buffer = DataBuffer::Create(size);
    buffer->Assign(data, size);
    return Send(std::move(host), port, buffer);
}

bool UdpServerImpl::Send(std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0 || host.empty()) {
        return false;
    }

    if (!isRunning_.load()) {
        LMNET_LOGE("UDP server not running");
        return false;
    }

    // Setup destination address (IPv4/IPv6)
    bool dst_ipv6 = (host.find(':') != std::string::npos);
    sockaddr_storage dst{};
    int dstLen = 0;
    if (dst_ipv6) {
        sockaddr_in6 a6{};
        a6.sin6_family = AF_INET6;
        a6.sin6_port = htons(port);
        if (inet_pton(AF_INET6, host.c_str(), &a6.sin6_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET6) failed for address: %s", host.c_str());
            return false;
        }
        memcpy(&dst, &a6, sizeof(a6));
        dstLen = sizeof(a6);
    } else {
        sockaddr_in a4{};
        a4.sin_family = AF_INET;
        a4.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &a4.sin_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET) failed for address: %s", host.c_str());
            return false;
        }
        memcpy(&dst, &a4, sizeof(a4));
        dstLen = sizeof(a4);
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitSendToRequest(
        socket_, buffer, reinterpret_cast<const sockaddr *>(&dst), dstLen, [self](SOCKET socket, DWORD bytesOrError) {
            if (self->taskQueue_) {
                auto task =
                    std::make_shared<TaskHandler<void>>([self, bytesOrError]() { self->HandleSend(bytesOrError); });
                self->taskQueue_->EnqueueTask(task);
            } else {
                self->HandleSend(bytesOrError);
            }
        });

    if (!success) {
        LMNET_LOGE("Failed to submit UDP send request to %s:%u", host.c_str(), port);
        return false;
    }

    return true;
}

bool UdpServerImpl::Send(std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        return false;
    }
    return Send(std::move(host), port, str.data(), str.size());
}

void UdpServerImpl::HandleSend(DWORD bytesOrError)
{
    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("UDP send error: %lu", bytesOrError);
    }
    // For successful sends, we don't need to do anything special
}

socket_t UdpServerImpl::GetSocketFd() const
{
    return (socket_t)socket_;
}

} // namespace lmshao::lmnet
