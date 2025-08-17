/**
 * @file tcp_server_impl.cpp
 * @brief Windows TCP Server IOCP implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_server_impl.h"

#include <mswsock.h>
#include <ws2tcpip.h>

#include "iocp_manager.h"
#include "iocp_utils.h"
#include "network_log.h"
#include "session.h"
#include "session_impl.h"
#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace lmshao::network {
struct PerIoContextTCP {
    OVERLAPPED ov{};
    WSABUF buf{};
    char data[8192];
    DWORD bytes = 0;
    enum class Op {
        RECV,
        SEND,
        ACCEPT
    } op{Op::RECV};
    SOCKET sock = INVALID_SOCKET;         // active connection socket for RECV/SEND
    SOCKET acceptSocket = INVALID_SOCKET; // new socket for ACCEPT op
    PerIoContextTCP()
    {
        ZeroMemory(&ov, sizeof(ov));
        buf.buf = data;
        buf.len = (ULONG)sizeof(data);
    }
};

class TcpServerState {
public:
    SOCKET listenSocket = INVALID_SOCKET;
    bool running = false;
    std::mutex mtx;
    std::unordered_map<SOCKET, std::shared_ptr<Session>> sessions;
    LPFN_ACCEPTEX fnAcceptEx = nullptr;
    LPFN_GETACCEPTEXSOCKADDRS fnGetAddrs = nullptr;
};

TcpServerImpl::TcpServerImpl(std::string listenIp, uint16_t listenPort) : ip_(std::move(listenIp)), port_(listenPort) {}
TcpServerImpl::TcpServerImpl(uint16_t listenPort) : port_(listenPort) {}

bool TcpServerImpl::Init()
{
    EnsureWsaInit();
    state_ = std::make_shared<TcpServerState>();
    state_->listenSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (state_->listenSocket == INVALID_SOCKET) {
        NETWORK_LOGE("WSASocket listen failed: %d", WSAGetLastError());
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (ip_.empty())
        ip_ = "0.0.0.0";
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        NETWORK_LOGE("inet_pton failed");
        return false;
    }
    if (bind(state_->listenSocket, (sockaddr *)&addr, sizeof(addr)) != 0) {
        NETWORK_LOGE("bind failed: %d", WSAGetLastError());
        return false;
    }
    if (listen(state_->listenSocket, SOMAXCONN) != 0) {
        NETWORK_LOGE("listen failed: %d", WSAGetLastError());
        return false;
    }

    // Initialize global IOCP manager
    auto manager = IocpManager::GetInstance();
    if (!manager->Initialize()) {
        NETWORK_LOGE("Failed to initialize IOCP manager");
        return false;
    }

    // Register socket with global IOCP
    if (!manager->RegisterSocket((HANDLE)state_->listenSocket, (ULONG_PTR)state_->listenSocket,
                                 std::shared_ptr<IIocpHandler>(this, [](IIocpHandler *) {}))) {
        NETWORK_LOGE("Associate listen socket to IOCP failed");
        return false;
    }
    // Load AcceptEx / GetAcceptExSockaddrs
    GUID guidAcceptEx = WSAID_ACCEPTEX;
    GUID guidGetAddrs = WSAID_GETACCEPTEXSOCKADDRS;
    DWORD bytes = 0;
    if (WSAIoctl(state_->listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx),
                 &state_->fnAcceptEx, sizeof(state_->fnAcceptEx), &bytes, nullptr, nullptr) == SOCKET_ERROR ||
        !state_->fnAcceptEx) {
        NETWORK_LOGE("WSAIoctl AcceptEx load failed: %d", WSAGetLastError());
        return false;
    }
    if (WSAIoctl(state_->listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidGetAddrs, sizeof(guidGetAddrs),
                 &state_->fnGetAddrs, sizeof(state_->fnGetAddrs), &bytes, nullptr, nullptr) == SOCKET_ERROR ||
        !state_->fnGetAddrs) {
        NETWORK_LOGE("WSAIoctl GetAcceptExSockaddrs load failed: %d", WSAGetLastError());
        return false;
    }
    return true;
}

void TcpServerImpl::SetListener(std::shared_ptr<IServerListener> listener)
{
    listener_ = listener;
}

bool TcpServerImpl::Start()
{
    if (!state_ || state_->running) {
        return false;
    }
    state_->running = true;
    PostAccept();
    return true;
}

bool TcpServerImpl::Stop()
{
    if (!state_ || !state_->running) {
        return true;
    }
    state_->running = false;

    // Unregister socket from global IOCP
    IocpManager::GetInstance()->UnregisterSocket((ULONG_PTR)state_->listenSocket);

    return true;
}

void TcpServerImpl::OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error)
{
    if (!ov) {
        return;
    }

    auto *ctx = reinterpret_cast<PerIoContextTCP *>(ov);
    if (!state_->running) {
        delete ctx;
        return;
    }

    if (!success) {
        NETWORK_LOGE("IOCP operation failed, error: %lu", error);
        delete ctx;
        return;
    }

    switch (ctx->op) {
        case PerIoContextTCP::Op::ACCEPT:
            HandleAccept(ctx, bytes);
            break;
        case PerIoContextTCP::Op::RECV:
            HandleRecv(ctx, bytes);
            break;
        case PerIoContextTCP::Op::SEND: /* future */
            delete ctx;
            break;
    }
}

socket_t TcpServerImpl::GetSocketFd() const
{
    return state_ ? (socket_t)state_->listenSocket : -1;
}

void TcpServerImpl::PostAccept()
{
    auto *ctx = new PerIoContextTCP();
    ctx->op = PerIoContextTCP::Op::ACCEPT;
    ctx->acceptSocket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (ctx->acceptSocket == INVALID_SOCKET) {
        delete ctx;
        return;
    }
    DWORD bytes = 0;
    // Buffer: local + remote addresses + initial data 0
    ctx->buf.len = 0; // we manage raw buffer ourselves
    BOOL r = state_->fnAcceptEx(state_->listenSocket, ctx->acceptSocket, ctx->data, 0, sizeof(sockaddr_in) + 16,
                                sizeof(sockaddr_in) + 16, &bytes, &ctx->ov);
    if (!r) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
            NETWORK_LOGE("AcceptEx failed: %d", err);
            closesocket(ctx->acceptSocket);
            delete ctx;
            return;
        }
    }
}

void TcpServerImpl::PostRecv(std::shared_ptr<SessionImpl> session)
{
    auto *ctx = new PerIoContextTCP();
    ctx->op = PerIoContextTCP::Op::RECV;
    ctx->sock = (SOCKET)session->fd;
    DWORD flags = 0;
    DWORD bytes = 0;
    int r = WSARecv(ctx->sock, &ctx->buf, 1, &bytes, &flags, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            NETWORK_LOGE("WSARecv failed: %d", err);
            delete ctx;
            return;
        }
    }
}

void TcpServerImpl::HandleAccept(PerIoContextTCP *ctx, DWORD)
{
    if (!state_->running) {
        closesocket(ctx->acceptSocket);
        delete ctx;
        return;
    } // associate new socket
    IocpManager::GetInstance()->RegisterSocket((HANDLE)ctx->acceptSocket, (ULONG_PTR)ctx->acceptSocket,
                                               std::shared_ptr<IIocpHandler>(this, [](IIocpHandler *) {}));
    // Extract addresses
    sockaddr *localSock = nullptr, *remoteSock = nullptr;
    int localLen = 0, remoteLen = 0;
    state_->fnGetAddrs(ctx->data, 0, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, &localSock, &localLen,
                       &remoteSock, &remoteLen);
    std::string host;
    uint16_t port = 0;
    if (remoteSock) {
        auto *rin = reinterpret_cast<sockaddr_in *>(remoteSock);
        char addrStr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &rin->sin_addr, addrStr, INET_ADDRSTRLEN) != nullptr) {
            host = addrStr;
        }
        port = ntohs(rin->sin_port);
    }
    auto session = std::make_shared<SessionImpl>((socket_t)ctx->acceptSocket, host, port, shared_from_this());
    {
        std::lock_guard<std::mutex> lk(state_->mtx);
        state_->sessions[ctx->acceptSocket] = session;
    }
    if (listener_) {
        listener_->OnAccept(session);
    }
    PostRecv(session);
    delete ctx;
    PostAccept();
}

void TcpServerImpl::HandleRecv(PerIoContextTCP *ctx, DWORD bytes)
{
    auto sock = ctx->sock;
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lk(state_->mtx);
        auto it = state_->sessions.find(sock);
        if (it != state_->sessions.end())
            session = it->second;
    }
    if (bytes == 0) {
        if (session && listener_) {
            listener_->OnClose(session);
        }
        if (session) {
            std::lock_guard<std::mutex> lk(state_->mtx);
            state_->sessions.erase(sock);
        }
        closesocket(sock);
        delete ctx;
        return;
    }
    if (listener_ && session) {
        auto buf = DataBuffer::Create(bytes);
        buf->Assign(ctx->data, bytes);
        listener_->OnReceive(session, buf);
    }
    // repost recv
    if (session) {
        PostRecv(std::static_pointer_cast<SessionImpl>(session));
    }
    delete ctx;
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
{
    (void)host;
    (void)port; // unused in TCP
    if (!data || size == 0 || fd == INVALID_SOCKET) {
        return false;
    }
    int ret = ::send((SOCKET)fd, (const char *)data, (int)size, 0);
    return ret != SOCKET_ERROR;
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0) {
        return false;
    }
    return Send(fd, std::move(host), port, buffer->Data(), buffer->Size());
}

bool TcpServerImpl::Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
{
    if (str.empty()) {
        return false;
    }
    return Send(fd, std::move(host), port, str.data(), str.size());
}

} // namespace lmshao::network
