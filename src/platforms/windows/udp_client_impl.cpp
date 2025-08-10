/**
 * @file udp_client_impl.cpp
 * @brief Windows UDP Client IOCP implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "iocp_utils.h"
#include "log.h"
#pragma comment(lib, "ws2_32.lib")

#include <cstring>
#include <memory>

namespace lmshao::network {

using PerIoContext = win::UdpPerIoContext;

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
}

UdpClientImpl::~UdpClientImpl()
{
    Close();
}

bool UdpClientImpl::Init()
{
    win::EnsureWsaInit();
    WSADATA __wsa_tmp{};
    WSAStartup(MAKEWORD(2, 2), &__wsa_tmp);
    socket_ = ::WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("WSASocket failed: %d", WSAGetLastError());
        return false;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(localPort_);
        if (localIp_.empty())
            localIp_ = "0.0.0.0";
        if (inet_pton(AF_INET, localIp_.c_str(), &localAddr.sin_addr) != 1) {
            NETWORK_LOGE("inet_pton local failed: %d", WSAGetLastError());
            Close();
            return false;
        }
        if (bind(socket_, reinterpret_cast<sockaddr *>(&localAddr), sizeof(localAddr)) != 0) {
            NETWORK_LOGE("bind failed: %d", WSAGetLastError());
            Close();
            return false;
        }
    } else {
        // For UDP client, we need to bind to receive replies
        // Bind to any available port
        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr.sin_port = 0; // Let system choose port
        if (bind(socket_, reinterpret_cast<sockaddr *>(&localAddr), sizeof(localAddr)) != 0) {
            NETWORK_LOGE("Auto-bind failed: %d", WSAGetLastError());
            Close();
            return false;
        }
    }

    std::memset(&remoteAddr_, 0, sizeof(remoteAddr_));
    remoteAddr_.sin_family = AF_INET;
    remoteAddr_.sin_port = htons(remotePort_);
    if (inet_pton(AF_INET, remoteIp_.c_str(), &remoteAddr_.sin_addr) != 1) {
        NETWORK_LOGE("inet_pton remote failed: %d", WSAGetLastError());
        Close();
        return false;
    }

    // Associate IOCP + start worker + post recvs
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_) {
        NETWORK_LOGE("CreateIoCompletionPort failed: %lu", GetLastError());
        Close();
        return false;
    }
    if (!CreateIoCompletionPort((HANDLE)socket_, (HANDLE)iocp_, (ULONG_PTR)this, 0)) {
        NETWORK_LOGE("Associate socket IOCP failed: %lu", GetLastError());
        Close();
        return false;
    }
    running_ = true;
    StartWorker();
    for (int i = 0; i < 4; ++i)
        PostRecv();
    NETWORK_LOGD("UDP client (IOCP) initialized: %s:%u", remoteIp_.c_str(), remotePort_);
    return true;
}

bool UdpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET || len == 0)
        return false;
    // Async send using overlapped WSASendTo
    auto *ctx = new PerIoContext();
    ctx->op = PerIoContext::Op::SEND;
    size_t copyLen = len < sizeof(ctx->data) ? len : sizeof(ctx->data);
    std::memcpy(ctx->data, data, copyLen);
    ctx->wsaBuf.buf = ctx->data;
    ctx->wsaBuf.len = (ULONG)copyLen;
    DWORD bytes = 0;
    int r = WSASendTo(socket_, &ctx->wsaBuf, 1, &bytes, 0, (sockaddr *)&remoteAddr_, sizeof(remoteAddr_), &ctx->ov,
                      nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            if (listener_)
                listener_->OnError(socket_, "WSASendTo error: " + std::to_string(err));
            NETWORK_LOGE("WSASendTo failed: %d", err);
            delete ctx;
            return false;
        }
    }
    return true;
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data)
        return false;
    return Send(data->Data(), data->Size());
}

void UdpClientImpl::Close()
{
    StopWorker();
    if (socket_ != INVALID_SOCKET) {
        ::closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    if (iocp_) {
        CloseHandle((HANDLE)iocp_);
        iocp_ = nullptr;
    }
}

void UdpClientImpl::PostRecv()
{
    if (!running_ || socket_ == INVALID_SOCKET)
        return;
    auto *ctx = new PerIoContext();
    ZeroMemory(&ctx->ov, sizeof(ctx->ov));
    ctx->wsaBuf.buf = ctx->data;
    ctx->wsaBuf.len = (ULONG)sizeof(ctx->data);
    ctx->fromLen = sizeof(ctx->from);
    ctx->op = PerIoContext::Op::RECV;

    DWORD flags = 0;
    DWORD bytes = 0;
    int r =
        WSARecvFrom(socket_, &ctx->wsaBuf, 1, &bytes, &flags, (sockaddr *)&ctx->from, &ctx->fromLen, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            NETWORK_LOGE("WSARecvFrom failed: %d", err);
            if (listener_)
                listener_->OnError(socket_, "WSARecvFrom error: " + std::to_string(err));
            delete ctx;
        }
    }
}

void UdpClientImpl::StartWorker()
{
    worker_ = std::thread([this] { WorkerLoop(); });
}

void UdpClientImpl::StopWorker()
{
    if (running_) {
        running_ = false;
        PostQueuedCompletionStatus((HANDLE)iocp_, 0, 0, nullptr);
    }
    if (worker_.joinable())
        worker_.join();
}

void UdpClientImpl::WorkerLoop()
{
    while (true) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL ok = GetQueuedCompletionStatus((HANDLE)iocp_, &bytes, &key, &pov, INFINITE);
        if (!ok) {
            int gle = GetLastError();
            if (!pov) { // possibly wake-up / shutdown
                if (!running_)
                    break;
                continue;
            }
            if (listener_)
                listener_->OnError(socket_, "GQCS error: " + std::to_string(gle));
        }
        if (!pov) {
            if (!running_)
                break;
            continue;
        }
        auto *ctx = reinterpret_cast<PerIoContext *>(pov);
        if (!running_) {
            delete ctx;
            break;
        }
        if (ctx->op == PerIoContext::Op::SEND) {
            // nothing to do on success; errors already reported earlier
            delete ctx;
            continue;
        }
        // RECV op
        if (bytes == 0) { // zero-length datagram or error; recycle recv
            delete ctx;
            PostRecv();
            continue;
        }
        if (listener_) {
            auto buf = DataBuffer::Create(bytes);
            buf->Assign(ctx->data, bytes);
            listener_->OnReceive(socket_, buf);
        }
        delete ctx;
        PostRecv();
    }
    if (listener_)
        listener_->OnClose(socket_);
}

void UdpClientImpl::HandleReceive(socket_t fd)
{
    (void)fd;
}
void UdpClientImpl::HandleConnectionClose(socket_t fd, bool error, const std::string &info)
{
    (void)fd;
    (void)error;
    (void)info;
}

} // namespace lmshao::network
