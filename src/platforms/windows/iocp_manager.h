/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_IOCP_MANAGER_H
#define LMSHAO_LMNET_IOCP_MANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// clang-format off
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>

#include "lmcore/data_buffer.h"
#include "lmcore/singleton.h"
#include "lmnet/common.h"
// clang-format on

namespace lmshao::lmnet {

enum class IocpRequestType {
    ACCEPT,
    CONNECT,
    READ,
    WRITE,
    CLOSE,
    RECVFROM,
    SENDTO,
    EXIT
};

using ConnectCallback = std::function<void(SOCKET, DWORD)>;                      // socket, error
using AcceptCallback = std::function<void(SOCKET, SOCKET, const sockaddr_in &)>; // listen_fd, client_fd, addr
using ReadCallback =
    std::function<void(SOCKET, std::shared_ptr<lmcore::DataBuffer>, DWORD)>; // socket, buffer, bytes/error
using WriteCallback = std::function<void(SOCKET, DWORD)>;                    // socket, bytes/error
using CloseCallback = std::function<void(SOCKET, DWORD)>;                    // socket, result
using RecvFromCallback = std::function<void(SOCKET, std::shared_ptr<lmcore::DataBuffer>, DWORD,
                                            const sockaddr_in &)>; // socket, buffer, bytes/error, addr
using SendToCallback = std::function<void(SOCKET, DWORD)>;         // socket, bytes/error

/**
 * Self-contained request structure - no external dependencies
 * Similar to io_uring's Request but optimized for IOCP
 */
struct IocpRequest {
    OVERLAPPED overlapped{};
    IocpRequestType type;
    SOCKET socket{INVALID_SOCKET};

    // Callback functions
    ConnectCallback connect_cb;
    AcceptCallback accept_cb;
    ReadCallback read_cb;
    WriteCallback write_cb;
    CloseCallback close_cb;
    RecvFromCallback recvfrom_cb;
    SendToCallback sendto_cb;

    // Data buffer management
    std::shared_ptr<lmcore::DataBuffer> buffer;

    // IOCP-specific data
    WSABUF wsaBuf{};
    sockaddr_in remoteAddr{};
    int remoteAddrLen{sizeof(sockaddr_in)};

    // Accept-specific data
    SOCKET acceptSocket{INVALID_SOCKET};
    char acceptBuffer[1024]; // For AcceptEx address data

    IocpRequest() : type(IocpRequestType::READ), remoteAddrLen(sizeof(sockaddr_in))
    {
        ZeroMemory(&overlapped, sizeof(overlapped));
        ZeroMemory(&remoteAddr, sizeof(remoteAddr));
    }

    // Clear the request for reuse
    void Clear()
    {
        ZeroMemory(&overlapped, sizeof(overlapped));
        type = IocpRequestType::READ;
        socket = INVALID_SOCKET;
        connect_cb = nullptr;
        accept_cb = nullptr;
        read_cb = nullptr;
        write_cb = nullptr;
        close_cb = nullptr;
        recvfrom_cb = nullptr;
        sendto_cb = nullptr;
        buffer.reset();
        wsaBuf = {};
        ZeroMemory(&remoteAddr, sizeof(remoteAddr));
        remoteAddrLen = sizeof(sockaddr_in);
        acceptSocket = INVALID_SOCKET;
        ZeroMemory(acceptBuffer, sizeof(acceptBuffer));
    }
};

class IocpManager : public lmcore::Singleton<IocpManager> {
public:
    ~IocpManager();

    bool Init(int entries = 512, int workerThreads = 4);
    void Stop();
    void Exit();

    // Performance statistics
    struct IocpStats {
        std::atomic<uint64_t> operations_submitted{0};
        std::atomic<uint64_t> operations_completed{0};
        std::atomic<uint64_t> buffer_reuses{0};
        std::atomic<uint64_t> request_pool_hits{0};
        std::atomic<uint64_t> request_pool_misses{0};
    };
    const IocpStats &GetStats() const { return stats_; }

    // Get IOCP handle for external socket association
    HANDLE GetIocpHandle() const { return iocp_; }

    // Unified async operations - similar to io_uring interface
    bool SubmitConnectRequest(SOCKET socket, const sockaddr_in &addr, ConnectCallback callback);
    bool SubmitAcceptRequest(SOCKET listenSocket, AcceptCallback callback);
    bool SubmitReadRequest(SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buffer, ReadCallback callback);
    bool SubmitWriteRequest(SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buffer, WriteCallback callback);
    bool SubmitRecvFromRequest(SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buffer, RecvFromCallback callback);
    bool SubmitSendToRequest(SOCKET socket, std::shared_ptr<lmcore::DataBuffer> buffer, const sockaddr_in &addr,
                             SendToCallback callback);
    bool SubmitCloseRequest(SOCKET socket, CloseCallback callback);

private:
    friend class lmcore::Singleton<IocpManager>;
    IocpManager() = default;

    void WorkerLoop();
    void HandleCompletion(IocpRequest *req, DWORD bytes, DWORD error);

    // Object pool management
    IocpRequest *GetRequest();
    void PutRequest(IocpRequest *req);

    // Helper functions for different operations
    bool SubmitOperation(IocpRequestType type, SOCKET socket, const std::function<void(IocpRequest *)> &init_request,
                         const std::function<bool(IocpRequest *)> &submit_operation);

    // Extension function pointers - loading
    bool LoadWinsockExtensions(SOCKET socket);

private:
    HANDLE iocp_{nullptr};
    int entries_{0};

    // Object pool for requests
    std::vector<IocpRequest> requestPool_;
    std::vector<IocpRequest *> freeRequests_;
    std::mutex poolMutex_;

    // Worker threads
    std::atomic<bool> isRunning_{false};
    std::vector<std::thread> workerThreads_;
    int workerThreadCount_{0};

    // Extension function pointers
    LPFN_CONNECTEX fnConnectEx_{nullptr};
    LPFN_ACCEPTEX fnAcceptEx_{nullptr};
    LPFN_GETACCEPTEXSOCKADDRS fnGetAcceptExSockaddrs_{nullptr};

    mutable IocpStats stats_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_IOCP_MANAGER_H
