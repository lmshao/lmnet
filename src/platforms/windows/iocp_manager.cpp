/**
 * Global IOCP Manager Implementation
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "iocp_manager.h"

#include <algorithm>

#include "../../internal_logger.h"

// Windows max/min macros conflict with std::min/std::max
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace lmshao::network {
IocpManager::~IocpManager()
{
    Shutdown();
}

bool IocpManager::Initialize(int workerThreads)
{
    if (running_.load()) {
        return true; // Already initialized
    }

    // Create IOCP
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!iocp_) {
        NETWORK_LOGE("Failed to create IOCP: %lu", GetLastError());
        return false;
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
    running_.store(true);
    shutdown_.store(false);

    workers_.reserve(workerThreadCount_);
    for (int i = 0; i < workerThreadCount_; ++i) {
        workers_.emplace_back([this] { WorkerLoop(); });
    }

    NETWORK_LOGD("IOCP Manager initialized with %d worker threads", workerThreadCount_);
    return true;
}

void IocpManager::Shutdown()
{
    if (!running_.load()) {
        return; // Already shut down
    }

    NETWORK_LOGD("IOCP Manager shutting down...");

    // Signal shutdown
    running_.store(false);
    shutdown_.store(true);

    // Wake up all worker threads
    for (size_t i = 0; i < workers_.size(); ++i) {
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
    }

    // Wait for all workers to finish
    for (auto &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
    CleanupResources();

    NETWORK_LOGD("IOCP Manager shutdown complete");
}

bool IocpManager::RegisterSocket(HANDLE socket, ULONG_PTR key, std::shared_ptr<IIocpHandler> handler)
{
    if (!running_.load()) {
        NETWORK_LOGE("IOCP Manager not running");
        return false;
    }

    if (!handler) {
        NETWORK_LOGE("Handler cannot be null");
        return false;
    }

    // Associate socket with IOCP
    HANDLE result = CreateIoCompletionPort(socket, iocp_, key, 0);
    if (!result) {
        NETWORK_LOGE("Failed to associate socket with IOCP: %lu", GetLastError());
        return false;
    }

    // Register handler
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handlers_[key] = std::move(handler);
    }

    NETWORK_LOGD("Socket registered with IOCP Manager, key: %llu", static_cast<unsigned long long>(key));
    return true;
}

void IocpManager::UnregisterSocket(ULONG_PTR key)
{
    std::lock_guard<std::mutex> lock(handlerMutex_);
    auto it = handlers_.find(key);
    if (it != handlers_.end()) {
        handlers_.erase(it);
        NETWORK_LOGD("Socket unregistered from IOCP Manager, key: %llu", static_cast<unsigned long long>(key));
    }
}

void IocpManager::WorkerLoop()
{
    NETWORK_LOGD("IOCP worker thread started");

    while (running_.load()) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL success = GetQueuedCompletionStatus(iocp_, &bytes, &key, &overlapped, INFINITE);
        DWORD error = success ? 0 : GetLastError();

        NETWORK_LOGD("IOCP event received: success=%d, bytes=%lu, key=%llu, overlapped=%p, error=%lu", success, bytes,
                     static_cast<unsigned long long>(key), overlapped, error);

        // Check for shutdown signal
        if (!running_.load() && !overlapped) {
            NETWORK_LOGD("IOCP worker thread shutting down");
            break;
        }

        // Handle completion
        if (overlapped || success) {
            std::shared_ptr<IIocpHandler> handler;

            // Find handler for this key
            {
                std::lock_guard<std::mutex> lock(handlerMutex_);
                auto it = handlers_.find(key);
                if (it != handlers_.end()) {
                    handler = it->second;
                }
            }

            if (handler) {
                try {
                    handler->OnIoCompletion(key, overlapped, bytes, success == TRUE, error);
                } catch (const std::exception &e) {
                    NETWORK_LOGE("Exception in IOCP handler: %s", e.what());
                } catch (...) {
                    NETWORK_LOGE("Unknown exception in IOCP handler");
                }
            } else if (overlapped) {
                NETWORK_LOGW("No handler found for IOCP completion, key: %llu", static_cast<unsigned long long>(key));
            }
        }

        // Handle errors without overlapped (usually critical errors)
        if (!success && !overlapped && running_.load()) {
            if (error == WAIT_TIMEOUT) {
                continue;
            } else {
                NETWORK_LOGE("IOCP GetQueuedCompletionStatus failed: %lu", error);
                // Continue running but log the error
            }
        }
    }

    NETWORK_LOGD("IOCP worker thread exiting");
}

void IocpManager::CleanupResources()
{
    // Clear handlers
    {
        std::lock_guard<std::mutex> lock(handlerMutex_);
        handlers_.clear();
    }

    // Close IOCP handle
    if (iocp_) {
        CloseHandle(iocp_);
        iocp_ = nullptr;
    }
}

} // namespace lmshao::network
