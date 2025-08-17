/**
 * @file iocp_manager.h
 * @brief Global IOCP Manager - Centralized IOCP management for all sockets
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_IOCP_MANAGER_H
#define NETWORK_IOCP_MANAGER_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common.h"
#include "core-utils/singleton.h"

namespace lmshao::network {

// Forward declaration for IOCP completion callback
class IIocpHandler {
public:
    virtual ~IIocpHandler() = default;
    virtual void OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error) = 0;
};

/**
 * @brief Global IOCP Manager - Singleton pattern for centralized socket management
 *
 * This manager provides centralized IOCP handling for all UDP/TCP clients and servers.
 * Benefits:
 * - Reduced thread overhead (shared worker threads)
 * - Better resource utilization
 * - Simplified socket lifecycle management
 * - Consistent error handling
 */
class IocpManager : public Singleton<IocpManager> {
public:
    ~IocpManager();

    // Initialize the IOCP manager with specified worker thread count
    bool Initialize(int workerThreads = 0); // 0 = CPU core count

    // Shutdown the manager and all worker threads
    void Shutdown();

    // Associate a socket with the IOCP and register a handler
    bool RegisterSocket(HANDLE socket, ULONG_PTR key, std::shared_ptr<IIocpHandler> handler);

    // Unregister a socket and its handler
    void UnregisterSocket(ULONG_PTR key);

    // Get the IOCP handle for manual operations (if needed)
    HANDLE GetIocpHandle() const { return iocp_; }

    // Check if the manager is running
    bool IsRunning() const { return running_.load(); }

private:
    friend class Singleton<IocpManager>;
    IocpManager() = default;

    void WorkerLoop();
    void CleanupResources();

    HANDLE iocp_{nullptr};
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdown_{false};
    std::vector<std::thread> workers_;

    // Thread-safe handler registry
    std::mutex handlerMutex_;
    std::unordered_map<ULONG_PTR, std::shared_ptr<IIocpHandler>> handlers_;

    // Worker thread management
    int workerThreadCount_{0};
};

} // namespace lmshao::network

#endif // NETWORK_IOCP_MANAGER_H
