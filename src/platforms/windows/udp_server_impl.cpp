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
#include "session_impl.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

UdpServerImpl::UdpServerImpl(std::string listenIp, uint16_t listenPort) : ip_(std::move(listenIp)), port_(listenPort)
{
    taskQueue_ = std::make_unique<lmcore::TaskQueue>("UdpServer");
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

    // Create UDP socket
    socket_ = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }

    // Setup listen address
    listenAddr_.sin_family = AF_INET;
    listenAddr_.sin_port = htons(port_);
    if (ip_.empty()) {
        ip_ = "0.0.0.0";
    }
    if (inet_pton(AF_INET, ip_.c_str(), &listenAddr_.sin_addr) != 1) {
        LMNET_LOGE("inet_pton failed");
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    // Bind
    if (bind(socket_, (sockaddr *)&listenAddr_, sizeof(listenAddr_)) != 0) {
        LMNET_LOGE("bind failed: %d", WSAGetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    // Associate socket with IOCP
    auto &iocpManager = IocpManager::GetInstance();
    if (!CreateIoCompletionPort((HANDLE)socket_, iocpManager.GetIocpHandle(), (ULONG_PTR)socket_, 0)) {
        LMNET_LOGE("Failed to associate UDP server socket with IOCP: %lu", GetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    LMNET_LOGD("UDP server initialized on %s:%u", ip_.c_str(), port_);
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
        // Retry after a short delay in case of temporary issues
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (isRunning_.load()) {
            if (taskQueue_) {
                auto task = std::make_shared<lmcore::TaskHandler<void>>([self]() { self->SubmitReceive(); });
                taskQueue_->EnqueueTask(task);
            } else {
                SubmitReceive();
            }
        }
    }
}

void UdpServerImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError, const sockaddr_in &fromAddr)
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
        // Continue receiving despite error
        SubmitReceive();
        return;
    }

    // Valid data received - extract client info
    char addrStr[INET_ADDRSTRLEN];
    std::string host;
    uint16_t port = 0;

    if (inet_ntop(AF_INET, &fromAddr.sin_addr, addrStr, INET_ADDRSTRLEN) != nullptr) {
        host = addrStr;
    }
    port = ntohs(fromAddr.sin_port);

    // Create a temporary session for this UDP packet
    // Note: UDP is connectionless, so we create ephemeral sessions
    auto session = std::make_shared<SessionImpl>((socket_t)socket_, host, port, shared_from_this());

    // Notify listener
    if (auto listener = listener_.lock()) {
        listener->OnReceive(session, buffer);
    }

    // Continue receiving
    SubmitReceive();
}

bool UdpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
{
    if (!data || size == 0) {
        return false;
    }

    auto buffer = lmcore::DataBuffer::Create(size);
    buffer->Assign(data, size);
    return Send(fd, std::move(host), port, buffer);
}

bool UdpServerImpl::Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0 || host.empty()) {
        return false;
    }

    if (!isRunning_.load()) {
        LMNET_LOGE("UDP server not running");
        return false;
    }

    // Setup destination address
    sockaddr_in destAddr{};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &destAddr.sin_addr) != 1) {
        LMNET_LOGE("inet_pton failed for address: %s", host.c_str());
        return false;
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitSendToRequest(socket_, buffer, destAddr, [self](SOCKET socket, DWORD bytesOrError) {
        if (self->taskQueue_) {
            auto task =
                std::make_shared<lmcore::TaskHandler<void>>([self, bytesOrError]() { self->HandleSend(bytesOrError); });
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

bool UdpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        return false;
    }
    return Send(fd, std::move(host), port, str.data(), str.size());
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
