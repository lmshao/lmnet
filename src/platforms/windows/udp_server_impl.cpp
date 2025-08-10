/**
 * @file udp_server_impl.cpp
 * @brief Windows UDP Server IOCP implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_server_impl.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include "iocp_utils.h"
#include "log.h"
#include "session.h"
#pragma comment(lib, "ws2_32.lib")

#include <cstring>

namespace lmshao::network {

// ------------- IOCP based implementation -------------
class UdpSessionWin final : public Session {
public:
    UdpSessionWin(const sockaddr_in &addr, socket_t s, UdpServerImpl *server) : server_(server)
    {
        fd = (int)s;
        char addrStr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &addr.sin_addr, addrStr, INET_ADDRSTRLEN) != nullptr) {
            host = addrStr;
        }
        port = ntohs(addr.sin_port);
        // Store client address for sending responses
        clientAddr_ = addr;
    }
    bool Send(std::shared_ptr<DataBuffer> data) const override
    {
        if (!data || !server_)
            return false;
        return server_->SendTo(clientAddr_, data->Data(), data->Size());
    }
    bool Send(const std::string &str) const override
    {
        if (!server_)
            return false;
        return server_->SendTo(clientAddr_, str.data(), str.size());
    }
    bool Send(const void *data, size_t len) const override
    {
        if (!server_)
            return false;
        return server_->SendTo(clientAddr_, data, len);
    }
    std::string ClientInfo() const override { return host + ":" + std::to_string(port); }

private:
    UdpServerImpl *server_;
    sockaddr_in clientAddr_;
};

using PerIoContext = win::UdpPerIoContext;

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port) : ip_(std::move(ip)), port_(port) {}
UdpServerImpl::~UdpServerImpl()
{
    Stop();
    CloseSocket();
    if (iocp_) {
        CloseHandle((HANDLE)iocp_);
        iocp_ = nullptr;
    }
}

bool UdpServerImpl::Init()
{
    win::EnsureWsaInit();
    WSADATA __wsa_tmp{};
    WSAStartup(MAKEWORD(2, 2), &__wsa_tmp);
    socket_ = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("WSASocket failed: %d", (int)WSAGetLastError());
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    if (ip_.empty())
        ip_ = "0.0.0.0";
    if (inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        NETWORK_LOGE("inet_pton failed for %s", ip_.c_str());
        CloseSocket();
        return false;
    }
    if (bind(socket_, (sockaddr *)&addr, sizeof(addr)) != 0) {
        NETWORK_LOGE("bind failed: %d", (int)WSAGetLastError());
        CloseSocket();
        return false;
    }
    return true;
}

bool UdpServerImpl::Start()
{
    if (running_)
        return true;
    if (!socket_)
        return false;
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_) {
        NETWORK_LOGE("CreateIoCompletionPort failed: %d", (int)GetLastError());
        return false;
    }
    if (!CreateIoCompletionPort((HANDLE)socket_, (HANDLE)iocp_, (ULONG_PTR)this, 0)) {
        NETWORK_LOGE("Associate socket IOCP failed: %d", (int)GetLastError());
        return false;
    }
    running_ = true;
    for (int i = 0; i < 4; ++i)
        PostRecv();
    worker_ = std::thread([this] { WorkerLoop(); });
    return true;
}

bool UdpServerImpl::Stop()
{
    if (!running_)
        return true;
    running_ = false;
    PostQueuedCompletionStatus((HANDLE)iocp_, 0, 0, nullptr);
    if (worker_.joinable())
        worker_.join();
    return true;
}

void UdpServerImpl::CloseSocket()
{
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, const void *data, size_t len)
{
    (void)fd;
    if (socket_ == INVALID_SOCKET)
        return false;
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &to.sin_addr) != 1)
        return false;
    int ret = sendto(socket_, (const char *)data, (int)len, 0, (sockaddr *)&to, sizeof(to));
    return ret != SOCKET_ERROR;
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, const std::string &str)
{
    return Send(fd, std::move(ip), port, str.data(), str.size());
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    if (!data)
        return false;
    return Send(fd, std::move(ip), port, data->Data(), data->Size());
}

bool UdpServerImpl::SendTo(const sockaddr_in &to, const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET || !data || len == 0)
        return false;
    int ret = sendto(socket_, (const char *)data, (int)len, 0, (const sockaddr *)&to, sizeof(to));
    return ret != SOCKET_ERROR;
}

void UdpServerImpl::PostRecv()
{
    if (socket_ == INVALID_SOCKET)
        return;
    auto *ctx = new PerIoContext();
    ZeroMemory(&ctx->ov, sizeof(ctx->ov));
    ctx->wsaBuf.buf = ctx->data;
    ctx->wsaBuf.len = (ULONG)sizeof(ctx->data);
    ctx->fromLen = sizeof(ctx->from);
    DWORD flags = 0;
    DWORD bytes = 0;
    int r =
        WSARecvFrom(socket_, &ctx->wsaBuf, 1, &bytes, &flags, (sockaddr *)&ctx->from, &ctx->fromLen, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            NETWORK_LOGE("WSARecvFrom failed: %d", (int)err);
            delete ctx;
        }
    }
}

void UdpServerImpl::WorkerLoop()
{
    while (true) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;
        BOOL ok = GetQueuedCompletionStatus((HANDLE)iocp_, &bytes, &key, &pov, INFINITE);
        if (!ok) {
            int gle = GetLastError();
            if (!pov) { // wake or shutdown
                if (!running_)
                    break;
                continue;
            }
            if (listener_) {
                auto dummy = std::make_shared<UdpSessionWin>(ctxLastFrom_, socket_, this);
                listener_->OnError(dummy, "GQCS error: " + std::to_string(gle));
            }
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
        if (bytes == 0) { // zero length datagram; recycle
            delete ctx;
            PostRecv();
            continue;
        }
        ctxLastFrom_ = ctx->from;                  // remember last peer for error callbacks
        HandlePacket(ctx->data, bytes, ctx->from); // deliver
        delete ctx;
        PostRecv();
    }
    if (listener_) {
        auto dummy = std::make_shared<UdpSessionWin>(ctxLastFrom_, socket_, this);
        listener_->OnClose(dummy);
    }
}

void UdpServerImpl::HandlePacket(const char *data, size_t len, const sockaddr_in &from)
{
    if (!listener_)
        return;
    auto buf = DataBuffer::Create(len);
    buf->Assign(data, len);
    auto session = std::make_shared<UdpSessionWin>(from, socket_, this);
    listener_->OnReceive(session, buf);
}

} // namespace lmshao::network
