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

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"
#include "lmcore/task_queue.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

TcpClientImpl::TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<lmcore::TaskQueue>("TcpClient");
}

TcpClientImpl::~TcpClientImpl()
{
    Close();

    // Give sufficient time for pending IOCP callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

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

    // Create new socket
    socket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket failed: %d", WSAGetLastError());
        return;
    }

    // Setup local binding if specified
    if (!localIp_.empty() || localPort_ != 0) {
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_pton(AF_INET, localIp_.c_str(), &localAddr_.sin_addr);

        if (bind(socket_, (sockaddr *)&localAddr_, sizeof(localAddr_)) != 0) {
            LMNET_LOGE("bind failed: %d", WSAGetLastError());
            return;
        }
    } else {
        // ConnectEx requires the socket to be bound first
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr_.sin_port = 0; // Let system choose port

        if (bind(socket_, (sockaddr *)&localAddr_, sizeof(localAddr_)) != 0) {
            LMNET_LOGE("Auto-bind for ConnectEx failed: %d", WSAGetLastError());
            return;
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

    // Setup server address
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    if (inet_pton(AF_INET, remoteIp_.c_str(), &serverAddr_.sin_addr) != 1) {
        LMNET_LOGE("inet_pton remote failed");
        return;
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

    std::unique_lock<std::mutex> lock(connectMutex_);
    bool completed = connectCond_.wait_for(lock, std::chrono::seconds(5), [this]() { return !connectPending_; });
    if (!completed) {
        LMNET_LOGE("Connect to %s:%u timed out", remoteIp_.c_str(), remotePort_);
        connectPending_ = false;
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

    bool success = manager.SubmitConnectRequest(socket_, serverAddr_, [self](SOCKET socket, DWORD error) {
        if (self->taskQueue_) {
            auto task = std::make_shared<lmcore::TaskHandler<void>>([self, error]() { self->HandleConnect(error); });
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

    auto buffer = lmcore::DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitReadRequest(
        socket_, buffer, [self, buffer](SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buf, DWORD bytesOrError) {
            if (self->taskQueue_) {
                auto task = std::make_shared<lmcore::TaskHandler<void>>(
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

    auto buffer = lmcore::DataBuffer::Create(len);
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
            auto task =
                std::make_shared<lmcore::TaskHandler<void>>([self, bytesOrError]() { self->HandleSend(bytesOrError); });
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
