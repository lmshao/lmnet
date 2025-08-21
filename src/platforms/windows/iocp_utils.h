/**
 * Common IOCP utilities shared by Windows UDP server & client
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_IOCP_UTILS_H
#define LMSHAO_NETWORK_IOCP_UTILS_H

#include "network/common.h"

namespace lmshao::network {

// Global WSA startup / cleanup using singleton pattern
struct WsaGlobalInit {
    WsaGlobalInit()
    {
        WSADATA d{};
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaGlobalInit() { WSACleanup(); }
};

// Ensure WSA initialization using local static (singleton pattern)
inline void EnsureWsaInit()
{
    static WsaGlobalInit g_wsaInit; // single global instance (C++11 thread-safe)
    (void)g_wsaInit;
}

// Per I/O overlapped context for UDP recv/send (currently only recv used)
struct UdpPerIoContext {
    OVERLAPPED ov{};
    WSABUF wsaBuf{};
    char data[4096]{};  // buffer
    sockaddr_in from{}; // peer address (recv)
    int fromLen = sizeof(from);
    DWORD bytes = 0; // completion bytes
    enum class Op {
        RECV,
        SEND
    } op{Op::RECV};
    UdpPerIoContext()
    {
        ZeroMemory(&ov, sizeof(ov));
        wsaBuf.buf = data;
        wsaBuf.len = (ULONG)sizeof(data);
        fromLen = sizeof(from);
    }
};

// Helper to post one overlapped WSARecvFrom. Returns 0 on success / pending; or WSA error code.
inline int PostUdpRecv(SOCKET s, UdpPerIoContext *ctx)
{
    DWORD flags = 0;
    DWORD bytes = 0;
    int r = WSARecvFrom(s, &ctx->wsaBuf, 1, &bytes, &flags, (sockaddr *)&ctx->from, &ctx->fromLen, &ctx->ov, nullptr);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            return err;
        }
    }
    return 0; // success or pending
}

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_IOCP_UTILS_H
