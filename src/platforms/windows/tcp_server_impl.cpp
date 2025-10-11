/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_server_impl.h"

#include <mswsock.h>
#include <ws2tcpip.h>

#define SO_UPDATE_ACCEPT_CONTEXT 0x700B // If not defined

#include "internal_logger.h"
#include "iocp_manager.h"
#include "iocp_utils.h"
#include "session_impl.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

TcpServerImpl::TcpServerImpl(std::string listenIp, uint16_t listenPort) : ip_(std::move(listenIp)), port_(listenPort)
{
    taskQueue_ = std::make_unique<lmcore::TaskQueue>("TcpServer");
}

TcpServerImpl::TcpServerImpl(uint16_t listenPort) : TcpServerImpl("0.0.0.0", listenPort) {}

TcpServerImpl::~TcpServerImpl()
{
    Stop();

    // Give sufficient time for pending IOCP callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (taskQueue_) {
        taskQueue_->Stop();
    }
}

bool TcpServerImpl::Init()
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

    // Create listen socket
    listenSocket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket listen failed: %d", WSAGetLastError());
        taskQueue_->Stop(); // Clean up on failure
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
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    // Bind
    if (bind(listenSocket_, (sockaddr *)&listenAddr_, sizeof(listenAddr_)) != 0) {
        LMNET_LOGE("bind failed: %d", WSAGetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    // Listen
    if (listen(listenSocket_, SOMAXCONN) != 0) {
        LMNET_LOGE("listen failed: %d", WSAGetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    // Associate listen socket with IOCP
    auto &iocpManager = IocpManager::GetInstance();
    if (!CreateIoCompletionPort((HANDLE)listenSocket_, iocpManager.GetIocpHandle(), (ULONG_PTR)listenSocket_, 0)) {
        LMNET_LOGE("Failed to associate listen socket with IOCP: %lu", GetLastError());
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
        return false;
    }

    LMNET_LOGD("TCP server initialized on %s:%u", ip_.c_str(), port_);
    return true;
}

void TcpServerImpl::SetListener(std::shared_ptr<IServerListener> listener)
{
    listener_ = listener;
}

bool TcpServerImpl::Start()
{
    if (isRunning_.load()) {
        LMNET_LOGW("Server already running");
        return true;
    }

    if (listenSocket_ == INVALID_SOCKET) {
        LMNET_LOGE("Server not initialized");
        return false;
    }

    isRunning_.store(true);

    // Start accepting connections
    SubmitAccept();

    LMNET_LOGI("TCP server started on %s:%u", ip_.c_str(), port_);
    return true;
}

bool TcpServerImpl::Stop()
{
    if (!isRunning_.load()) {
        return true;
    }

    LMNET_LOGI("Stopping TCP server");

    isRunning_.store(false);

    // Close all client sessions
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        for (auto &[socket, session] : sessions_) {
            closesocket(socket);
        }
        sessions_.clear();
    }

    // Close listen socket
    if (listenSocket_ != INVALID_SOCKET) {
        closesocket(listenSocket_);
        listenSocket_ = INVALID_SOCKET;
    }

    LMNET_LOGI("TCP server stopped");
    return true;
}

void TcpServerImpl::SubmitAccept()
{
    if (!isRunning_.load()) {
        return;
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitAcceptRequest(
        listenSocket_, [self](SOCKET listenSocket, SOCKET clientSocket, const sockaddr_in &clientAddr) {
            if (clientSocket != INVALID_SOCKET) {
                if (self->taskQueue_) {
                    auto task = std::make_shared<lmcore::TaskHandler<void>>(
                        [self, clientSocket, clientAddr]() { self->HandleAccept(clientSocket, clientAddr); });
                    self->taskQueue_->EnqueueTask(task);
                } else {
                    self->HandleAccept(clientSocket, clientAddr);
                }
            }

            // Continue accepting new connections
            self->SubmitAccept();
        });

    if (!success) {
        LMNET_LOGE("Failed to submit accept request");
        // Retry after a short delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SubmitAccept();
    }
}

void TcpServerImpl::HandleAccept(SOCKET clientSocket, const sockaddr_in &clientAddr)
{
    if (!isRunning_.load()) {
        closesocket(clientSocket);
        return;
    }

    // Update accept socket context - required after AcceptEx
    if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&listenSocket_, sizeof(listenSocket_)) ==
        SOCKET_ERROR) {
        LMNET_LOGE("setsockopt SO_UPDATE_ACCEPT_CONTEXT failed: %d", WSAGetLastError());
        closesocket(clientSocket);
        return;
    }

    // Associate accepted socket with IOCP for future overlapped operations
    auto &manager = IocpManager::GetInstance();
    if (!CreateIoCompletionPort((HANDLE)clientSocket, manager.GetIocpHandle(), (ULONG_PTR)clientSocket, 0)) {
        LMNET_LOGE("Failed to associate client socket with IOCP: %lu", GetLastError());
        closesocket(clientSocket);
        return;
    }

    // Extract client address info
    char addrStr[INET_ADDRSTRLEN];
    std::string host;
    uint16_t port = 0;

    if (inet_ntop(AF_INET, &clientAddr.sin_addr, addrStr, INET_ADDRSTRLEN) != nullptr) {
        host = addrStr;
    }
    port = ntohs(clientAddr.sin_port);

    // Create session
    auto session = std::make_shared<SessionImpl>((socket_t)clientSocket, host, port, shared_from_this());
    AddSession(clientSocket, session);

    LMNET_LOGD("New client connected: %s:%u (socket=%llu)", host.c_str(), port,
               static_cast<unsigned long long>(clientSocket));

    // Notify listener
    if (auto listener = listener_.lock()) {
        listener->OnAccept(session);
    }

    // Start reading from client
    SubmitRead(clientSocket);
}

void TcpServerImpl::SubmitRead(SOCKET clientSocket)
{
    if (!isRunning_.load()) {
        return;
    }

    auto buffer = lmcore::DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitReadRequest(
        clientSocket, buffer,
        [self, clientSocket, buffer](SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buf, DWORD bytesOrError) {
            if (self->taskQueue_) {
                auto task = std::make_shared<lmcore::TaskHandler<void>>([self, clientSocket, buf, bytesOrError]() {
                    self->HandleReceive(clientSocket, buf, bytesOrError);
                });
                self->taskQueue_->EnqueueTask(task);
            } else {
                self->HandleReceive(clientSocket, buf, bytesOrError);
            }
        });

    if (!success) {
        LMNET_LOGE("Failed to submit read request for client socket %llu",
                   static_cast<unsigned long long>(clientSocket));
        if (taskQueue_) {
            auto task = std::make_shared<lmcore::TaskHandler<void>>([self, clientSocket]() {
                self->HandleClientClose(clientSocket, true, "Failed to submit read request");
            });
            taskQueue_->EnqueueTask(task);
        } else {
            HandleClientClose(clientSocket, true, "Failed to submit read request");
        }
    }
}

void TcpServerImpl::HandleReceive(SOCKET clientSocket, std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError)
{
    if (bytesOrError == 0) {
        // Connection closed by peer
        LMNET_LOGD("Client socket %llu closed connection", static_cast<unsigned long long>(clientSocket));
        HandleClientClose(clientSocket, false, "Connection closed by peer");
        return;
    }

    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("Receive error on socket %llu: %lu", static_cast<unsigned long long>(clientSocket), bytesOrError);
        HandleClientClose(clientSocket, true, "Receive error: " + std::to_string(bytesOrError));
        return;
    }

    // Valid data received
    auto session = GetSession(clientSocket);
    if (session) {
        if (auto listener = listener_.lock()) {
            listener->OnReceive(session, buffer);
        }
    }

    // Continue reading
    SubmitRead(clientSocket);
}

void TcpServerImpl::HandleClientClose(SOCKET clientSocket, bool isError, const std::string &reason)
{
    LMNET_LOGD("Handling client close: socket=%llu, error=%d, reason=%s", static_cast<unsigned long long>(clientSocket),
               isError, reason.c_str());

    auto session = GetSession(clientSocket);
    if (session) {
        // Notify listener
        if (auto listener = listener_.lock()) {
            if (isError) {
                // For server, we don't have a specific OnError callback for sessions
                // Just treat it as a close
                listener->OnClose(session);
            } else {
                listener->OnClose(session);
            }
        }

        // Remove session
        RemoveSession(clientSocket);
    }

    // Close socket
    closesocket(clientSocket);
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
{
    (void)host; // Not used for TCP
    (void)port; // Not used for TCP

    if (!data || size == 0 || fd == INVALID_SOCKET) {
        return false;
    }

    auto buffer = lmcore::DataBuffer::Create(size);
    buffer->Assign(data, size);
    return Send(fd, "", 0, buffer);
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    (void)host; // Not used for TCP
    (void)port; // Not used for TCP

    if (!buffer || buffer->Size() == 0 || fd == INVALID_SOCKET) {
        return false;
    }

    if (!isRunning_.load()) {
        LMNET_LOGE("Server not running");
        return false;
    }

    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitWriteRequest((SOCKET)fd, buffer, [self, fd](SOCKET socket, DWORD bytesOrError) {
        self->HandleSend((SOCKET)fd, bytesOrError);
    });

    if (!success) {
        LMNET_LOGE("Failed to submit write request for socket %d", fd);
        return false;
    }

    return true;
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        return false;
    }
    return Send(fd, std::move(host), port, str.data(), str.size());
}

void TcpServerImpl::HandleSend(SOCKET clientSocket, DWORD bytesOrError)
{
    if (bytesOrError > 65536) { // Assume it's an error code
        LMNET_LOGE("Send error on socket %llu: %lu", static_cast<unsigned long long>(clientSocket), bytesOrError);
        HandleClientClose(clientSocket, true, "Send error: " + std::to_string(bytesOrError));
    }
    // For successful sends, we don't need to do anything special
}

void TcpServerImpl::AddSession(SOCKET socket, std::shared_ptr<Session> session)
{
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessions_[socket] = session;
}

void TcpServerImpl::RemoveSession(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(sessionMutex_);
    sessions_.erase(socket);
}

std::shared_ptr<Session> TcpServerImpl::GetSession(SOCKET socket)
{
    std::lock_guard<std::mutex> lock(sessionMutex_);
    auto it = sessions_.find(socket);
    return (it != sessions_.end()) ? it->second : nullptr;
}

socket_t TcpServerImpl::GetSocketFd() const
{
    return (socket_t)listenSocket_;
}

} // namespace lmshao::lmnet
