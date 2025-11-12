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
#include "tcp_session_impl.h"

#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

TcpServerImpl::TcpServerImpl(std::string listenIp, uint16_t listenPort) : ip_(std::move(listenIp)), port_(listenPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpServer");
}

TcpServerImpl::TcpServerImpl(uint16_t listenPort) : TcpServerImpl("0.0.0.0", listenPort) {}

TcpServerImpl::~TcpServerImpl()
{
    Stop();

    // Give time for pending IOCP callbacks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

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

    // Decide address family by IP string (':' implies IPv6)
    use_ipv6_ = (ip_.find(':') != std::string::npos);
    if (ip_.empty()) {
        // Default to IPv4 if not specified
        ip_ = "0.0.0.0";
        use_ipv6_ = false;
    }

    // Create listen socket with chosen family
    int family = use_ipv6_ ? AF_INET6 : AF_INET;
    listenSocket_ = WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listenSocket_ == INVALID_SOCKET) {
        LMNET_LOGE("WSASocket listen failed: %d", WSAGetLastError());
        taskQueue_->Stop(); // Clean up on failure
        return false;
    }

    // Enable dual stack for IPv6 socket to accept IPv4-mapped addresses
    if (use_ipv6_) {
        int v6only = 0;
        if (setsockopt(listenSocket_, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&v6only, sizeof(v6only)) == SOCKET_ERROR) {
            LMNET_LOGW("setsockopt IPV6_V6ONLY failed: %d", WSAGetLastError());
        }
    }

    // Setup listen address
    if (!use_ipv6_) {
        listenAddr_.sin_family = AF_INET;
        listenAddr_.sin_port = htons(port_);
        if (inet_pton(AF_INET, ip_.c_str(), &listenAddr_.sin_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET) failed");
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
    } else {
        ZeroMemory(&listenAddr6_, sizeof(listenAddr6_));
        listenAddr6_.sin6_family = AF_INET6;
        listenAddr6_.sin6_port = htons(port_);
        if (inet_pton(AF_INET6, ip_.c_str(), &listenAddr6_.sin6_addr) != 1) {
            LMNET_LOGE("inet_pton(AF_INET6) failed");
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return false;
        }
        // Bind
        if (bind(listenSocket_, (sockaddr *)&listenAddr6_, sizeof(listenAddr6_)) != 0) {
            LMNET_LOGE("bind(IPv6) failed: %d", WSAGetLastError());
            closesocket(listenSocket_);
            listenSocket_ = INVALID_SOCKET;
            return false;
        }
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

    bool success = manager.SubmitAcceptRequest(listenSocket_, [self](SOCKET listenSocket, SOCKET clientSocket) {
        if (clientSocket != INVALID_SOCKET) {
            if (self->taskQueue_) {
                auto task =
                    std::make_shared<TaskHandler<void>>([self, clientSocket]() { self->HandleAccept(clientSocket); });
                self->taskQueue_->EnqueueTask(task);
            } else {
                self->HandleAccept(clientSocket);
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

void TcpServerImpl::HandleAccept(SOCKET clientSocket)
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

    // Extract client address info using getpeername for IPv4/IPv6
    std::string host;
    uint16_t port = 0;
    sockaddr_storage peer{};
    int peerLen = sizeof(peer);
    if (getpeername(clientSocket, (sockaddr *)&peer, &peerLen) == 0) {
        if (peer.ss_family == AF_INET) {
            auto *in4 = reinterpret_cast<sockaddr_in *>(&peer);
            char addrStr4[INET_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET, &in4->sin_addr, addrStr4, sizeof(addrStr4)) != nullptr) {
                host.assign(addrStr4);
            }
            port = ntohs(in4->sin_port);
        } else if (peer.ss_family == AF_INET6) {
            auto *in6 = reinterpret_cast<sockaddr_in6 *>(&peer);
            char addrStr6[INET6_ADDRSTRLEN]{};
            if (inet_ntop(AF_INET6, &in6->sin6_addr, addrStr6, sizeof(addrStr6)) != nullptr) {
                host.assign(addrStr6);
            }
            port = ntohs(in6->sin6_port);
        }
    }

    // Create session
    auto session = std::make_shared<TcpSessionImpl>((socket_t)clientSocket, host, port, shared_from_this());
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

    auto buffer = DataBuffer::Create(8192);
    auto &manager = IocpManager::GetInstance();
    auto self = shared_from_this();

    bool success = manager.SubmitReadRequest(
        clientSocket, buffer,
        [self, clientSocket, buffer](SOCKET socket, std::shared_ptr<DataBuffer> buf, DWORD bytesOrError) {
            if (self->taskQueue_) {
                auto task = std::make_shared<TaskHandler<void>>([self, clientSocket, buf, bytesOrError]() {
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
            auto task = std::make_shared<TaskHandler<void>>([self, clientSocket]() {
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

bool TcpServerImpl::Send(socket_t fd, const void *data, size_t size)
{
    if (!data || size == 0 || fd == INVALID_SOCKET) {
        return false;
    }

    auto buffer = DataBuffer::Create(size);
    buffer->Assign(data, size);
    return Send(fd, buffer);
}

bool TcpServerImpl::Send(socket_t fd, std::shared_ptr<DataBuffer> buffer)
{
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

bool TcpServerImpl::Send(socket_t fd, const std::string &str)
{
    if (str.empty()) {
        return false;
    }
    auto buffer = DataBuffer::Create(str.size());
    buffer->Assign(str.data(), str.size());
    return Send(fd, buffer);
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
