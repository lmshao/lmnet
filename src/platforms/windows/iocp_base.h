/**
 * @file iocp_base.h
 * @brief Simple reusable IOCP base class
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_IOCP_BASE_H
#define NETWORK_IOCP_BASE_H

#include <atomic>
#include <thread>
#include <vector>

#include "common.h"

namespace lmshao::network::win {

class IocpBase {
public:
    IocpBase() = default;
    virtual ~IocpBase()
    {
        StopWorkers();
        CloseHandleSafe();
    }

    bool CreatePort()
    {
        if (iocp_)
            return true;
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        return iocp_ != nullptr;
    }

    bool Associate(HANDLE h, ULONG_PTR key) { return CreateIoCompletionPort(h, iocp_, key, 0) != nullptr; }

    bool StartWorkers(int n = 1)
    {
        if (running_)
            return true;
        if (!iocp_ && !CreatePort())
            return false;
        running_ = true;
        for (int i = 0; i < n; ++i) {
            workers_.emplace_back([this] { WorkerLoop(); });
        }
        return true;
    }

    void StopWorkers()
    {
        if (!running_)
            return;
        running_ = false;
        // wake threads
        for (size_t i = 0; i < workers_.size(); ++i) {
            PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
        }
        for (auto &t : workers_)
            if (t.joinable())
                t.join();
        workers_.clear();
    }

    HANDLE GetHandle() const { return iocp_; }
    bool IsRunning() const { return running_; }

protected:
    virtual void OnIocp(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool ok, DWORD err) = 0;

private:
    void WorkerLoop()
    {
        while (true) {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED pov = nullptr;
            BOOL ok = GetQueuedCompletionStatus(iocp_, &bytes, &key, &pov, INFINITE);
            DWORD gle = ok ? 0 : GetLastError();
            if (!running_ && !pov)
                break;
            if (!pov) {
                if (!running_)
                    break;
                else
                    continue;
            }
            OnIocp(key, pov, bytes, ok == TRUE, gle);
        }
    }

    void CloseHandleSafe()
    {
        if (iocp_) {
            CloseHandle(iocp_);
            iocp_ = nullptr;
        }
    }

    HANDLE iocp_{nullptr};
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;
};

} // namespace lmshao::network::win

#endif // NETWORK_IOCP_BASE_H
