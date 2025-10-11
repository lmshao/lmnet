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

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<lmcore::TaskQueue>("UdpClient");
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

    // Create UDP socket
    socket_ = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }

    // Setup local binding
    if (!localIp_.empty() || localPort_ != 0) {
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        if (inet_pton(AF_INET, localIp_.c_str(), &localAddr_.sin_addr) != 1) {
            LMNET_LOGE("inet_pton local failed: %d", WSAGetLastError());
            Close();
            return false;
        }

        if (bind(socket_, (sockaddr *)&localAddr_, sizeof(localAddr_)) != 0) {
            LMNET_LOGE("bind failed: %d", WSAGetLastError());
            Close();
            return false;
        }
    } else {
        // For UDP client, we need to bind to receive replies
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr_.sin_port = 0; // Let system choose port

        if (bind(socket_, (sockaddr *)&localAddr_, sizeof(localAddr_)) != 0) {
            LMNET_LOGE("Auto-bind failed: %d", WSAGetLastError());
            Close();
            return false;
        }
    }

    // Associate socket with IOCP
    if (!CreateIoCompletionPort((HANDLE)socket_, manager.GetIocpHandle(), (ULONG_PTR)socket_, 0)) {
        LMNET_LOGE("Failed to associate UDP client socket with IOCP: %lu", GetLastError());
        Close();
        return false;
    }

    // Setup remote address
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort_);
    if (inet_pton(AF_INET, remoteIp_.c_str(), &remoteAddr_.sin_addr) != 1) {
        LMNET_LOGE("inet_pton remote failed: %d", WSAGetLastError());
        Close();
        return false;
    }

    isRunning_.store(true);
    StartReceiving();

    LMNET_LOGD("UDP client initialized: %s:%u", remoteIp_.c_str(), remotePort_);
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

    auto buffer = lmcore::DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitRecvFromRequest(
        socket_, buffer,
        [self, buffer](SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buf, DWORD bytesOrError,
                       const sockaddr_in &fromAddr) {
            if (self->taskQueue_) {
                auto task = std::make_shared<lmcore::TaskHandler<void>>(
                    [self, buf, bytesOrError, fromAddr]() { self->HandleReceive(buf, bytesOrError, fromAddr); });
                self->taskQueue_->EnqueueTask(task);
            } else {
                self->HandleReceive(buf, bytesOrError, fromAddr);
            }
        });

    if (!success) {
        LMNET_LOGE("Failed to submit UDP receive request");
        if (taskQueue_) {
            auto task = std::make_shared<lmcore::TaskHandler<void>>(
                [self]() { self->HandleClose(true, "Failed to submit receive request"); });
            taskQueue_->EnqueueTask(task);
        } else {
            HandleClose(true, "Failed to submit receive request");
        }
    }
}

void UdpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError, const sockaddr_in &fromAddr)
{
    if (!isRunning_.load()) {
        return;
    }

    if (bytesOrError == 0) {
        // Zero-length datagram - just continue receiving
        SubmitReceive();
        return;
    }

    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("UDP receive error: %lu", bytesOrError);
        HandleClose(true, "Receive error: " + std::to_string(bytesOrError));
        return;
    }

    // Valid data received
    if (auto listener = listener_.lock()) {
        listener->OnReceive(socket_, buffer);
    }

    // Continue receiving
    SubmitReceive();
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

    auto buffer = lmcore::DataBuffer::Create(len);
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

    bool success = manager.SubmitSendToRequest(socket_, data, remoteAddr_, [self](SOCKET socket, DWORD bytesOrError) {
        if (self->taskQueue_) {
            auto task =
                std::make_shared<lmcore::TaskHandler<void>>([self, bytesOrError]() { self->HandleSend(bytesOrError); });
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
