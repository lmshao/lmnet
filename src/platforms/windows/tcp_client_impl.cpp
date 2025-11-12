/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_client_impl.h"

#include <ws2tcpip.h>

#include <chrono>
#include <cstdlib>

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

TcpClientImpl::TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpClient");
}

TcpClientImpl::~TcpClientImpl()
{
    Close();

    // Give time for pending IOCP callbacks to complete
    // Keep this minimal to avoid long delays when many clients are destroyed
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    if (taskQueue_) {
        taskQueue_->Stop();
    }
}

bool TcpClientImpl::Init()
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

    ReInit();
    isRunning_.store(true);

    LMNET_LOGD("TCP client initialized for %s:%u", remoteIp_.c_str(), remotePort_);
    return true;
}

void TcpClientImpl::ReInit()
{
    // Close existing socket if any
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
    }

    // Decide address family based on remote IP (':' implies IPv6)
    use_ipv6_ = (remoteIp_.find(':') != std::string::npos);

    // Create new socket
    int family = use_ipv6_ ? AF_INET6 : AF_INET;
    socket_ = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return;
    }

    // Setup local binding (ConnectEx requires a bound socket)
    if (!localIp_.empty() || localPort_ != 0) {
        if (!use_ipv6_) {
            sockaddr_in local4{};
            local4.sin_family = AF_INET;
            local4.sin_port = htons(localPort_);
            if (localIp_.empty()) {
                localIp_ = "0.0.0.0";
            }
            inet_pton(AF_INET, localIp_.c_str(), &local4.sin_addr);
            if (bind(socket_, (sockaddr *)&local4, sizeof(local4)) != 0) {
                LMNET_LOGE("bind failed: %d", WSAGetLastError());
                return;
            }
            memcpy(&localAddr_, &local4, sizeof(local4));
            localAddrLen_ = sizeof(local4);
        } else {
            sockaddr_in6 local6{};
            local6.sin6_family = AF_INET6;
            local6.sin6_port = htons(localPort_);
            if (localIp_.empty()) {
                localIp_ = "::";
            }
            if (inet_pton(AF_INET6, localIp_.c_str(), &local6.sin6_addr) != 1) {
                LMNET_LOGE("inet_pton local IPv6 failed");
                return;
            }
            if (bind(socket_, (sockaddr *)&local6, sizeof(local6)) != 0) {
                LMNET_LOGE("bind(IPv6) failed: %d", WSAGetLastError());
                return;
            }
            memcpy(&localAddr_, &local6, sizeof(local6));
            localAddrLen_ = sizeof(local6);
        }
    } else {
        if (!use_ipv6_) {
            sockaddr_in any4{};
            any4.sin_family = AF_INET;
            any4.sin_addr.s_addr = htonl(INADDR_ANY);
            any4.sin_port = 0; // Let system choose port
            if (bind(socket_, (sockaddr *)&any4, sizeof(any4)) != 0) {
                LMNET_LOGE("Auto-bind for ConnectEx failed: %d", WSAGetLastError());
                return;
            }
            memcpy(&localAddr_, &any4, sizeof(any4));
            localAddrLen_ = sizeof(any4);
        } else {
            sockaddr_in6 any6{};
            any6.sin6_family = AF_INET6;
            any6.sin6_addr = IN6ADDR_ANY_INIT;
            any6.sin6_port = 0; // Let system choose port
            if (bind(socket_, (sockaddr *)&any6, sizeof(any6)) != 0) {
                LMNET_LOGE("Auto-bind IPv6 for ConnectEx failed: %d", WSAGetLastError());
                return;
            }
            memcpy(&localAddr_, &any6, sizeof(any6));
            localAddrLen_ = sizeof(any6);
        }
    }

    // Associate socket with IOCP
    auto &manager = IocpManager::GetInstance();
    if (!CreateIoCompletionPort((HANDLE)socket_, manager.GetIocpHandle(), (ULONG_PTR)socket_, 0)) {
        LMNET_LOGE("Failed to associate client socket with IOCP: %lu", GetLastError());
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return;
    }

    // Setup server address (IPv4/IPv6)
    if (!use_ipv6_) {
        sockaddr_in srv4{};
        srv4.sin_family = AF_INET;
        srv4.sin_port = htons(remotePort_);
        if (inet_pton(AF_INET, remoteIp_.c_str(), &srv4.sin_addr) != 1) {
            LMNET_LOGE("inet_pton remote IPv4 failed");
            return;
        }
        memcpy(&serverAddr_, &srv4, sizeof(srv4));
        serverAddrLen_ = sizeof(srv4);
    } else {
        sockaddr_in6 srv6{};
        srv6.sin6_family = AF_INET6;
        srv6.sin6_port = htons(remotePort_);
        // Support link-local scope id: allow input like "fe80::xxxx%12"
        // Extract scope id (if any) and strip it from the address for inet_pton
        unsigned long scope_id = 0;
        std::string ip_no_scope = remoteIp_;
        auto percent_pos = ip_no_scope.find('%');
        if (percent_pos != std::string::npos) {
            std::string scope = ip_no_scope.substr(percent_pos + 1);
            ip_no_scope = ip_no_scope.substr(0, percent_pos);
            // Try numeric interface index; if not numeric, leave scope_id as 0
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
        if (inet_pton(AF_INET6, ip_no_scope.c_str(), &srv6.sin6_addr) != 1) {
            LMNET_LOGE("inet_pton remote IPv6 failed");
            return;
        }
        srv6.sin6_scope_id = scope_id; // Required for link-local addresses
        memcpy(&serverAddr_, &srv6, sizeof(srv6));
        serverAddrLen_ = sizeof(srv6);
    }

    isConnected_.store(false);
}

void TcpClientImpl::SetListener(std::shared_ptr<IClientListener> listener)
{
    listener_ = listener;
}

bool TcpClientImpl::Connect()
{
    if (!isRunning_.load()) {
        LMNET_LOGE("Client not initialized");
        return false;
    }

    if (isConnected_.load()) {
        LMNET_LOGW("Already connected");
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(connectMutex_);
        if (connectPending_) {
            LMNET_LOGW("Connect already in progress");
            return false;
        }
        connectPending_ = true;
        connectSuccess_ = false;
        lastConnectError_ = 0;
    }

    SubmitConnect();

    // Determine connect timeout (default 5000ms, override via env LMNET_CONNECT_TIMEOUT_MS)
    int timeoutMs = 5000;
    if (const char *env = std::getenv("LMNET_CONNECT_TIMEOUT_MS")) {
        long val = std::strtol(env, nullptr, 10);
        if (val > 0 && val < 600000) {
            timeoutMs = static_cast<int>(val);
        }
    }

    std::unique_lock<std::mutex> lock(connectMutex_);
    bool completed =
        connectCond_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this]() { return !connectPending_; });
    if (!completed) {
        // Treat timeout as a connect failure and notify listener
        LMNET_LOGE("Connect to %s:%u timed out after %d ms", remoteIp_.c_str(), remotePort_, timeoutMs);
        connectPending_ = false;
        connectSuccess_ = false;
        lastConnectError_ = WAIT_TIMEOUT;
        lock.unlock();
        HandleClose(true, "Connect timeout");
        return false;
    }

    if (!connectSuccess_) {
        if (lastConnectError_ != 0) {
            LMNET_LOGE("Connect to %s:%u failed with error %lu", remoteIp_.c_str(), remotePort_,
                       static_cast<unsigned long>(lastConnectError_));
        } else {
            LMNET_LOGE("Connect to %s:%u failed", remoteIp_.c_str(), remotePort_);
        }
        return false;
    }

    return true;
}

void TcpClientImpl::SubmitConnect()
{
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitConnectRequest(
        socket_, reinterpret_cast<const sockaddr *>(&serverAddr_), serverAddrLen_, [self](SOCKET socket, DWORD error) {
            if (self->taskQueue_) {
                auto task = std::make_shared<TaskHandler<void>>([self, error]() { self->HandleConnect(error); });
                self->taskQueue_->EnqueueTask(task);
            }
        });

    if (!success) {
        LMNET_LOGE("Failed to submit connect request");
        HandleClose(true, "Failed to submit connect request");
    }
}

void TcpClientImpl::HandleConnect(DWORD error)
{
    const bool success = (error == 0);

    {
        std::lock_guard<std::mutex> lock(connectMutex_);
        connectPending_ = false;
        connectSuccess_ = success;
        lastConnectError_ = error;
        isConnected_.store(success);
    }
    connectCond_.notify_all();

    if (!success) {
        LMNET_LOGE("Connect failed: %lu", error);
        HandleClose(true, "Connect failed: " + std::to_string(error));
        return;
    }

    LMNET_LOGD("Connected to %s:%u", remoteIp_.c_str(), remotePort_);

    // Start reading
    SubmitRead();
}

void TcpClientImpl::SubmitRead()
{
    if (!isRunning_.load() || !isConnected_.load()) {
        return;
    }

    auto buffer = DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitReadRequest(
        socket_, buffer, [self, buffer](SOCKET socket, std::shared_ptr<DataBuffer> buf, DWORD bytesOrError) {
            if (self->taskQueue_) {
                auto task = std::make_shared<TaskHandler<void>>(
                    [self, buf, bytesOrError]() { self->HandleReceive(buf, bytesOrError); });
                self->taskQueue_->EnqueueTask(task);
            }
        });

    if (!success) {
        LMNET_LOGE("Failed to submit read request");
        HandleClose(true, "Failed to submit read request");
    }
}

void TcpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError)
{
    if (bytesOrError == 0) {
        // Connection closed by peer
        LMNET_LOGD("Connection closed by peer");
        HandleClose(false, "Connection closed by peer");
        return;
    }

    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("Receive error: %lu", bytesOrError);
        HandleClose(true, "Receive error: " + std::to_string(bytesOrError));
        return;
    }

    // Valid data received
    if (auto listener = listener_.lock()) {
        listener->OnReceive(socket_, buffer);
    }

    // Continue reading
    SubmitRead();
}

bool TcpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool TcpClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        return false;
    }

    auto buffer = DataBuffer::Create(len);
    buffer->Assign(data, len);
    return Send(buffer);
}

bool TcpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        return false;
    }

    if (!isRunning_.load() || !isConnected_.load()) {
        LMNET_LOGE("Not connected");
        return false;
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitWriteRequest(socket_, data, [self](SOCKET socket, DWORD bytesOrError) {
        if (self->taskQueue_) {
            auto task = std::make_shared<TaskHandler<void>>([self, bytesOrError]() { self->HandleSend(bytesOrError); });
            self->taskQueue_->EnqueueTask(task);
        } else {
            self->HandleSend(bytesOrError);
        }
    });

    if (!success) {
        LMNET_LOGE("Failed to submit write request");
        return false;
    }

    return true;
}

void TcpClientImpl::HandleSend(DWORD bytesOrError)
{
    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("Send error: %lu", bytesOrError);
        HandleClose(true, "Send error: " + std::to_string(bytesOrError));
    }
    // For successful sends, we don't need to do anything special
}

void TcpClientImpl::Close()
{
    if (!isRunning_.load()) {
        return;
    }

    LMNET_LOGD("Closing TCP client");

    isRunning_.store(false);
    isConnected_.store(false);

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }

    {
        std::lock_guard<std::mutex> lock(connectMutex_);
        if (connectPending_) {
            connectPending_ = false;
            connectSuccess_ = false;
            lastConnectError_ = ERROR_OPERATION_ABORTED;
        }
    }
    connectCond_.notify_all();

    // Note: Don't stop or reset taskQueue_ here - let the destructor handle it
    // This avoids race conditions with IOCP worker threads still processing completions

    LMNET_LOGD("TCP client closed");
}

void TcpClientImpl::HandleClose(bool isError, const std::string &reason)
{
    if (!isRunning_.load()) {
        return;
    }

    LMNET_LOGD("Handling close: error=%d, reason=%s", isError, reason.c_str());

    isConnected_.store(false);

    {
        std::lock_guard<std::mutex> lock(connectMutex_);
        if (connectPending_) {
            connectPending_ = false;
            connectSuccess_ = false;
            lastConnectError_ = isError ? ERROR_OPERATION_ABORTED : 0;
        }
    }
    connectCond_.notify_all();

    // Notify listener
    if (auto listener = listener_.lock()) {
        if (isError) {
            listener->OnError(socket_, reason);
        } else {
            listener->OnClose(socket_);
        }
    }

    // Don't automatically reconnect - let the application decide
}

socket_t TcpClientImpl::GetSocketFd() const
{
    return socket_;
}

} // namespace lmshao::lmnet
