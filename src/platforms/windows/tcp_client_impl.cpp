/**
 * @file tcp_client_impl.cpp
 * @brief Windows TCP Client IOCP implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_client_impl.h"

#include <mswsock.h>
#include <ws2tcpip.h>

#include "iocp_manager.h"
#include "iocp_utils.h"
#include "network_log.h"
#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <thread>

namespace lmshao::network {

struct TcpClientPerIo {
    OVERLAPPED ov{};
    WSABUF buf{};
    char data[8192];
    DWORD bytes = 0;
    enum class Op {
        RECV,
        SEND,
        CONNECT
    } op{Op::RECV};
    TcpClientPerIo()
    {
        ZeroMemory(&ov, sizeof(ov));
        buf.buf = data;
        buf.len = (ULONG)sizeof(data);
    }
};

TcpClientImpl::TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
}

bool TcpClientImpl::Init()
{
    EnsureWsaInit();
    socket_ = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }
    if (!localIp_.empty() || localPort_ != 0) {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_port = htons(localPort_);
        if (localIp_.empty())
            localIp_ = "0.0.0.0";
        inet_pton(AF_INET, localIp_.c_str(), &local.sin_addr);
        if (bind(socket_, (sockaddr *)&local, sizeof(local)) != 0) {
            NETWORK_LOGE("bind failed: %d", WSAGetLastError());
            return false;
        }
    } else {
        // ConnectEx requires the socket to be bound first
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0; // Let system choose port
        if (bind(socket_, (sockaddr *)&local, sizeof(local)) != 0) {
            NETWORK_LOGE("Auto-bind for ConnectEx failed: %d", WSAGetLastError());
            return false;
        }
    }

    // Initialize global IOCP manager
    auto manager = IocpManager::GetInstance();
    if (!manager->Initialize()) {
        NETWORK_LOGE("Failed to initialize IOCP manager");
        return false;
    }

    // Register this socket with the global IOCP manager
    if (!manager->RegisterSocket((HANDLE)socket_, (ULONG_PTR)socket_, shared_from_this())) {
        NETWORK_LOGE("Failed to register socket with IOCP manager");
        return false;
    }

    running_ = true;
    LoadConnectEx();
    return true;
}

bool TcpClientImpl::LoadConnectEx()
{
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    int r = WSAIoctl(socket_, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &fnConnectEx,
                     sizeof(fnConnectEx), &bytes, nullptr, nullptr);
    if (r == SOCKET_ERROR || !fnConnectEx) {
        NETWORK_LOGE("Load ConnectEx failed: %d", WSAGetLastError());
        return false;
    }
    return true;
}

bool TcpClientImpl::Connect()
{
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(remotePort_);
    if (inet_pton(AF_INET, remoteIp_.c_str(), &remote.sin_addr) != 1) {
        NETWORK_LOGE("inet_pton remote failed");
        return false;
    }
    if (fnConnectEx) {
        auto *ctx = new TcpClientPerIo();
        ctx->op = TcpClientPerIo::Op::CONNECT;
        BOOL ok = fnConnectEx(socket_, (sockaddr *)&remote, sizeof(remote), nullptr, 0, nullptr, &ctx->ov);
        if (!ok) {
            int err = WSAGetLastError();
            if (err != ERROR_IO_PENDING) {
                NETWORK_LOGE("ConnectEx failed: %d", err);
                delete ctx;
                return false;
            }
        }
        return true; // completion will start recv
    } else {
        int r = ::connect(socket_, (sockaddr *)&remote, sizeof(remote));
        if (r != 0) {
            NETWORK_LOGE("connect failed: %d", WSAGetLastError());
            return false;
        }
        PostRecv();
        return true;
    }
}

void TcpClientImpl::SetListener(std::shared_ptr<IClientListener> l)
{
    listener_ = l;
}

bool TcpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}
bool TcpClientImpl::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET || len == 0) {
        return false;
    }
    PostSend(data, len);
    return true;
}
bool TcpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data) {
        return false;
    }
    return Send(data->Data(), data->Size());
}

void TcpClientImpl::PostSend(const void *data, size_t len)
{
    auto *ctx = new TcpClientPerIo();
    ctx->op = TcpClientPerIo::Op::SEND;
    size_t copy = len < sizeof(ctx->data) ? len : sizeof(ctx->data);
    std::memcpy(ctx->data, data, copy);
    ctx->buf.buf = ctx->data;
    ctx->buf.len = (ULONG)copy;
    DWORD bytes = 0;
    int r = WSASend(socket_, &ctx->buf, 1, &bytes, 0, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            if (listener_) {
                listener_->OnError(socket_, "WSASend error: " + std::to_string(err));
            }
            delete ctx;
        }
    }
}

void TcpClientImpl::PostRecv()
{
    auto *ctx = new TcpClientPerIo();
    ctx->op = TcpClientPerIo::Op::RECV;
    DWORD flags = 0;
    DWORD bytes = 0;
    int r = WSARecv(socket_, &ctx->buf, 1, &bytes, &flags, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            NETWORK_LOGE("WSARecv failed: %d", err);
            delete ctx;
        }
    }
}

void TcpClientImpl::Close()
{
    if (running_) {
        running_ = false;

        // Unregister from IOCP manager
        if (socket_ != INVALID_SOCKET) {
            IocpManager::GetInstance()->UnregisterSocket((ULONG_PTR)socket_);
        }
    }

    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void TcpClientImpl::OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error)
{
    if (!running_ || !ov) {
        return;
    }

    auto *ctx = reinterpret_cast<TcpClientPerIo *>(ov);

    if (!success) {
        if (listener_) {
            listener_->OnError(socket_, "IOCP completion error: " + std::to_string(error));
        }
        delete ctx;
        return;
    }

    switch (ctx->op) {
        case TcpClientPerIo::Op::CONNECT:
            // Connection established, start receiving
            delete ctx;
            PostRecv();
            break;

        case TcpClientPerIo::Op::SEND:
            // Send completion - just cleanup
            delete ctx;
            break;

        case TcpClientPerIo::Op::RECV:
            if (bytes == 0) {
                // Connection closed
                if (listener_) {
                    listener_->OnClose(socket_);
                }
                delete ctx;
                return;
            }

            if (listener_) {
                auto buf = DataBuffer::Create(bytes);
                buf->Assign(ctx->data, bytes);
                listener_->OnReceive(socket_, buf);
            }

            delete ctx;
            PostRecv(); // Continue receiving
            break;
    }
}

socket_t TcpClientImpl::GetSocketFd() const
{
    return socket_;
}

} // namespace lmshao::network
