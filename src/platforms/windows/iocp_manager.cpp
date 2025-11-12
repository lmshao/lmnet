/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "iocp_manager.h"

#include <mswsock.h>

#include <algorithm>

#include "internal_logger.h"

// Windows max/min macros conflict with std::min/std::max
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace lmshao::lmnet {

IocpManager::~IocpManager()
{
    Stop();
}

bool IocpManager::Init(int entries, int workerThreads)
{
    if (isRunning_.load()) {
        return true; // Already initialized
    }

    entries_ = entries;

    // Create IOCP
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_) {
        LMNET_LOGE("Failed to create IOCP: %lu", GetLastError());
        return false;
    }

    // Initialize request pool
    requestPool_.resize(entries_);
    freeRequests_.reserve(entries_);
    for (int i = 0; i < entries_; ++i) {
        freeRequests_.push_back(&requestPool_[i]);
    }

    // Determine worker thread count
    if (workerThreads <= 0) {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        workerThreadCount_ = static_cast<int>(sysInfo.dwNumberOfProcessors);
    } else {
        workerThreadCount_ = workerThreads;
    }

    // Limit thread count to reasonable bounds
    workerThreadCount_ = std::min(std::max(workerThreadCount_, 1), 64);

    // Start worker threads
    isRunning_.store(true);
    workerThreads_.reserve(workerThreadCount_);
    for (int i = 0; i < workerThreadCount_; ++i) {
        workerThreads_.emplace_back([this] { WorkerLoop(); });
    }

    LMNET_LOGI("IOCP Manager initialized with %d entries, %d worker threads", entries_, workerThreadCount_);
    return true;
}

void IocpManager::Stop()
{
    if (!isRunning_.load()) {
        return; // Already stopped
    }

    LMNET_LOGI("IOCP Manager stopping...");

    // Signal stop
    isRunning_.store(false);

    // Wake up all worker threads
    for (size_t i = 0; i < workerThreads_.size(); ++i) {
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
    }

    // Wait for all workers to finish
    for (auto &worker : workerThreads_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workerThreads_.clear();

    // Cleanup resources
    if (iocp_) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }

    // Clear object pool
    {
        std::lock_guard<std::mutex> lock(poolMutex_);
        freeRequests_.clear();
        requestPool_.clear();
    }

    LMNET_LOGI("IOCP Manager stopped");
}

void IocpManager::Exit()
{
    // Submit exit request
    IocpRequest *req = GetRequest();
    if (req) {
        req->type = IocpRequestType::EXIT;
        PostQueuedCompletionStatus(iocp_, 0, 0, &req->overlapped);
    }

    Stop();
}

IocpRequest *IocpManager::GetRequest()
{
    std::lock_guard<std::mutex> lock(poolMutex_);
    if (freeRequests_.empty()) {
        stats_.request_pool_misses.fetch_add(1);
        return nullptr;
    }

    IocpRequest *req = freeRequests_.back();
    freeRequests_.pop_back();
    req->Clear();
    stats_.request_pool_hits.fetch_add(1);
    return req;
}

void IocpManager::PutRequest(IocpRequest *req)
{
    if (req) {
        std::lock_guard<std::mutex> lock(poolMutex_);
        freeRequests_.push_back(req);
        stats_.buffer_reuses.fetch_add(1);
    }
}

void IocpManager::WorkerLoop()
{
    LMNET_LOGD("IOCP worker thread started");

    while (isRunning_.load()) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL success = GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, INFINITE);
        DWORD error = success ? 0 : GetLastError();

        // Check for shutdown signal
        if (!isRunning_.load() && !overlapped) {
            break;
        }

        if (overlapped) {
            IocpRequest *req = reinterpret_cast<IocpRequest *>(overlapped);

            // Check for exit request
            if (req->type == IocpRequestType::EXIT) {
                PutRequest(req);
                break;
            }

            // Handle completion
            try {
                HandleCompletion(req, bytes, error);
            } catch (const std::exception &e) {
                LMNET_LOGE("Exception in IOCP completion handler: %s", e.what());
            } catch (...) {
                LMNET_LOGE("Unknown exception in IOCP completion handler");
            }

            // Return request to pool
            PutRequest(req);

            // Update statistics
            stats_.operations_completed.fetch_add(1);
        }

        // Handle errors without overlapped
        if (!success && !overlapped && isRunning_.load()) {
            if (error == WAIT_TIMEOUT) {
                continue;
            } else {
                LMNET_LOGE("IOCP GetQueuedCompletionStatus failed: %lu", error);
            }
        }
    }

    LMNET_LOGD("IOCP worker thread exiting");
}

void IocpManager::HandleCompletion(IocpRequest *req, DWORD bytes, DWORD error)
{
    switch (req->type) {
        case IocpRequestType::CONNECT:
            if (req->connect_cb) {
                // Ensure the socket is transitioned to a connected state.
                // Windows requires SO_UPDATE_CONNECT_CONTEXT after ConnectEx succeeds.
                if (error == 0) {
                    int r = setsockopt(req->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
                    if (r != 0) {
                        LMNET_LOGW("SO_UPDATE_CONNECT_CONTEXT failed: %d", WSAGetLastError());
                    }
                }
                req->connect_cb(req->socket, error);
            }
            break;

        case IocpRequestType::ACCEPT:
            if (req->accept_cb) {
                req->accept_cb(req->socket, error == 0 ? req->acceptSocket : INVALID_SOCKET);
            }
            break;

        case IocpRequestType::READ:
            if (req->read_cb) {
                if (error == 0 && bytes > 0 && req->buffer) {
                    req->buffer->SetSize(bytes);
                }
                req->read_cb(req->socket, req->buffer, error == 0 ? bytes : error);
            }
            break;

        case IocpRequestType::WRITE:
        case IocpRequestType::SENDTO:
            if (req->write_cb) {
                req->write_cb(req->socket, error == 0 ? bytes : error);
            }
            if (req->sendto_cb) {
                req->sendto_cb(req->socket, error == 0 ? bytes : error);
            }
            break;

        case IocpRequestType::RECVFROM:
            if (req->recvfrom_cb) {
                if (error == 0 && bytes > 0 && req->buffer) {
                    req->buffer->SetSize(bytes);
                }
                req->recvfrom_cb(req->socket, req->buffer, error == 0 ? bytes : error, req->remoteAddr,
                                 req->remoteAddrLen);
            }
            break;

        case IocpRequestType::CLOSE:
            if (req->close_cb) {
                req->close_cb(req->socket, error);
            }
            break;

        case IocpRequestType::EXIT:
            // Handled in worker loop
            break;
    }
}

bool IocpManager::SubmitOperation(IocpRequestType type, SOCKET socket,
                                  const std::function<void(IocpRequest *)> &init_request,
                                  const std::function<bool(IocpRequest *)> &submit_operation)
{
    IocpRequest *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request from pool");
        return false;
    }

    req->type = type;
    req->socket = socket;

    if (init_request) {
        init_request(req);
    }

    bool success = false;
    if (submit_operation) {
        success = submit_operation(req);
    }

    if (!success) {
        PutRequest(req);
        return false;
    }

    stats_.operations_submitted.fetch_add(1);
    return true;
}

bool IocpManager::LoadWinsockExtensions(SOCKET socket)
{
    if (fnConnectEx_ && fnAcceptEx_ && fnGetAcceptExSockaddrs_) {
        return true; // Already loaded
    }

    DWORD bytes = 0;

    // Load ConnectEx
    if (!fnConnectEx_) {
        GUID guidConnectEx = WSAID_CONNECTEX;
        if (WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidConnectEx, sizeof(guidConnectEx), &fnConnectEx_,
                     sizeof(fnConnectEx_), &bytes, nullptr, nullptr) != 0) {
            LMNET_LOGE("Failed to load ConnectEx: %d", WSAGetLastError());
            return false;
        }
    }

    // Load AcceptEx
    if (!fnAcceptEx_) {
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        if (WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &fnAcceptEx_,
                     sizeof(fnAcceptEx_), &bytes, nullptr, nullptr) != 0) {
            LMNET_LOGE("Failed to load AcceptEx: %d", WSAGetLastError());
            return false;
        }
    }

    // Load GetAcceptExSockaddrs
    if (!fnGetAcceptExSockaddrs_) {
        GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
        if (WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidGetAcceptExSockaddrs,
                     sizeof(guidGetAcceptExSockaddrs), &fnGetAcceptExSockaddrs_, sizeof(fnGetAcceptExSockaddrs_),
                     &bytes, nullptr, nullptr) != 0) {
            LMNET_LOGE("Failed to load GetAcceptExSockaddrs: %d", WSAGetLastError());
            return false;
        }
    }

    return true;
}

bool IocpManager::SubmitConnectRequest(SOCKET socket, const sockaddr *addr, int addrLen, ConnectCallback callback)
{
    // Load ConnectEx if needed
    if (!LoadWinsockExtensions(socket)) {
        return false;
    }

    return SubmitOperation(
        IocpRequestType::CONNECT, socket,
        [callback, addr, addrLen](IocpRequest *req) {
            req->connect_cb = callback;
            ZeroMemory(&req->remoteAddr, sizeof(req->remoteAddr));
            int copyLen = std::min<int>(addrLen, (int)sizeof(req->remoteAddr));
            if (addr && copyLen > 0) {
                memcpy(&req->remoteAddr, addr, copyLen);
                req->remoteAddrLen = addrLen;
            }
        },
        [this, addr, addrLen](IocpRequest *req) {
            BOOL result =
                fnConnectEx_(req->socket, const_cast<sockaddr *>(addr), addrLen, nullptr, 0, nullptr, &req->overlapped);
            if (!result) {
                DWORD error = WSAGetLastError();
                return error == ERROR_IO_PENDING;
            }
            return true;
        });
}

bool IocpManager::SubmitAcceptRequest(SOCKET listenSocket, AcceptCallback callback)
{
    // Load AcceptEx if needed
    if (!LoadWinsockExtensions(listenSocket)) {
        return false;
    }

    return SubmitOperation(
        IocpRequestType::ACCEPT, listenSocket,
        [callback](IocpRequest *req) {
            req->accept_cb = callback;
            // Create accept socket with same address family as listen socket
            WSAPROTOCOL_INFOW protoInfo{};
            int len = sizeof(protoInfo);
            int r = getsockopt(req->socket, SOL_SOCKET, SO_PROTOCOL_INFOW, (char *)&protoInfo, &len);
            int af = (r == 0) ? protoInfo.iAddressFamily : AF_INET;
            req->acceptSocket = WSASocketW(af, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
        },
        [this](IocpRequest *req) {
            if (req->acceptSocket == INVALID_SOCKET) {
                LMNET_LOGE("Failed to create accept socket: %d", WSAGetLastError());
                return false;
            }

            DWORD bytes = 0;
            // Use large enough lengths for both local and remote address buffers
            int addrExt = sizeof(sockaddr_storage) + 16;
            BOOL result = fnAcceptEx_(req->socket, req->acceptSocket, req->acceptBuffer, 0, addrExt, addrExt, &bytes,
                                      &req->overlapped);
            if (!result) {
                DWORD error = WSAGetLastError();
                if (error != ERROR_IO_PENDING) {
                    closesocket(req->acceptSocket);
                    req->acceptSocket = INVALID_SOCKET;
                    return false;
                }
            }
            return true;
        });
}

bool IocpManager::SubmitReadRequest(SOCKET socket, std::shared_ptr<DataBuffer> buffer, ReadCallback callback)
{
    return SubmitOperation(
        IocpRequestType::READ, socket,
        [callback, buffer](IocpRequest *req) {
            req->read_cb = callback;
            req->buffer = buffer;
            req->wsaBuf.buf = reinterpret_cast<char *>(buffer->Data());
            req->wsaBuf.len = static_cast<ULONG>(buffer->Capacity());
        },
        [](IocpRequest *req) {
            DWORD flags = 0;
            DWORD bytes = 0;
            int result = WSARecv(req->socket, &req->wsaBuf, 1, &bytes, &flags, &req->overlapped, nullptr);
            if (result == SOCKET_ERROR) {
                DWORD error = WSAGetLastError();
                return error == WSA_IO_PENDING;
            }
            return true;
        });
}

bool IocpManager::SubmitWriteRequest(SOCKET socket, std::shared_ptr<DataBuffer> buffer, WriteCallback callback)
{
    return SubmitOperation(
        IocpRequestType::WRITE, socket,
        [callback, buffer](IocpRequest *req) {
            req->write_cb = callback;
            req->buffer = buffer;
            req->wsaBuf.buf = reinterpret_cast<char *>(buffer->Data());
            req->wsaBuf.len = static_cast<ULONG>(buffer->Size());
        },
        [](IocpRequest *req) {
            DWORD bytes = 0;
            int result = WSASend(req->socket, &req->wsaBuf, 1, &bytes, 0, &req->overlapped, nullptr);
            if (result == SOCKET_ERROR) {
                DWORD error = WSAGetLastError();
                return error == WSA_IO_PENDING;
            }
            return true;
        });
}

bool IocpManager::SubmitRecvFromRequest(SOCKET socket, std::shared_ptr<DataBuffer> buffer, RecvFromCallback callback)
{
    return SubmitOperation(
        IocpRequestType::RECVFROM, socket,
        [callback, buffer](IocpRequest *req) {
            req->recvfrom_cb = callback;
            req->buffer = buffer;
            req->wsaBuf.buf = reinterpret_cast<char *>(buffer->Data());
            req->wsaBuf.len = static_cast<ULONG>(buffer->Capacity());
            req->remoteAddrLen = sizeof(sockaddr_storage);
        },
        [](IocpRequest *req) {
            DWORD flags = 0;
            DWORD bytes = 0;
            int result = WSARecvFrom(req->socket, &req->wsaBuf, 1, &bytes, &flags, (sockaddr *)&req->remoteAddr,
                                     &req->remoteAddrLen, &req->overlapped, nullptr);
            if (result == SOCKET_ERROR) {
                DWORD error = WSAGetLastError();
                return error == WSA_IO_PENDING;
            }
            return true;
        });
}

bool IocpManager::SubmitSendToRequest(SOCKET socket, std::shared_ptr<DataBuffer> buffer, const sockaddr *addr,
                                      int addrLen, SendToCallback callback)
{
    return SubmitOperation(
        IocpRequestType::SENDTO, socket,
        [callback, buffer, addr, addrLen](IocpRequest *req) {
            req->sendto_cb = callback;
            req->buffer = buffer;
            req->wsaBuf.buf = reinterpret_cast<char *>(buffer->Data());
            req->wsaBuf.len = static_cast<ULONG>(buffer->Size());
            ZeroMemory(&req->remoteAddr, sizeof(req->remoteAddr));
            int copyLen = std::min<int>(addrLen, (int)sizeof(req->remoteAddr));
            if (addr && copyLen > 0) {
                memcpy(&req->remoteAddr, addr, copyLen);
                req->remoteAddrLen = addrLen;
            }
        },
        [](IocpRequest *req) {
            DWORD bytes = 0;
            int result = WSASendTo(req->socket, &req->wsaBuf, 1, &bytes, 0, (sockaddr *)&req->remoteAddr,
                                   req->remoteAddrLen, &req->overlapped, nullptr);
            if (result == SOCKET_ERROR) {
                DWORD error = WSAGetLastError();
                return error == WSA_IO_PENDING;
            }
            return true;
        });
}

bool IocpManager::SubmitCloseRequest(SOCKET socket, CloseCallback callback)
{
    return SubmitOperation(
        IocpRequestType::CLOSE, socket, [callback](IocpRequest *req) { req->close_cb = callback; },
        [](IocpRequest *req) {
            // For Windows, we can't really make close() asynchronous
            // So we just call closesocket and complete immediately
            int result = closesocket(req->socket);
            req->close_cb(req->socket, result == 0 ? 0 : WSAGetLastError());
            return false; // Don't submit to IOCP, already completed
        });
}

} // namespace lmshao::lmnet
