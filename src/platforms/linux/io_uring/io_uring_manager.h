/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef LMSHAO_LMNET_IO_URING_MANAGER_H
#define LMSHAO_LMNET_IO_URING_MANAGER_H

// This manager mirrors the responsibilities of the Windows IOCP manager while
// exposing an event-driven API compatible with existing Linux handlers.

#include <lmcore/singleton.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "lmnet/common.h"

struct io_uring;

namespace lmshao::lmnet {
using namespace lmshao::lmcore;

enum class EventType {
    READ = 0x01,
    WRITE = 0x02,
    ERROR = 0x04,
    CLOSE = 0x08
};

class EventHandler {
public:
    virtual ~EventHandler() = default;

    virtual void HandleRead(socket_t fd) = 0;
    virtual void HandleWrite(socket_t fd) = 0;
    virtual void HandleError(socket_t fd) = 0;
    virtual void HandleClose(socket_t fd) = 0;

    virtual int GetHandle() const = 0;
    virtual int GetEvents() const { return static_cast<int>(EventType::READ); }
};

class IoUringManager : public ManagedSingleton<IoUringManager> {
public:
    friend class ManagedSingleton<IoUringManager>;

    IoUringManager();
    ~IoUringManager();

    bool RegisterHandler(std::shared_ptr<EventHandler> handler);
    bool RemoveHandler(socket_t fd);
    bool ModifyHandler(socket_t fd, int events);

    void SetThreadName(const std::string &name);

    bool IsRunning() const { return running_.load(); }

private:
    struct HandlerEntry;

    struct RequestBase {
        enum class Kind {
            Poll,
            Remove,
            Wakeup
        };

        explicit RequestBase(Kind kind) : kind(kind) {}
        virtual ~RequestBase() = default;

        Kind kind;
    };

    struct PollRequest final : RequestBase {
        PollRequest(std::shared_ptr<HandlerEntry> entry, int fd, uint32_t events)
            : RequestBase(Kind::Poll), entry(std::move(entry)), fd(fd), events(events)
        {
        }

        std::shared_ptr<HandlerEntry> entry;
        int fd;
        uint32_t events;
    };

    struct RemoveRequest final : RequestBase {
        RemoveRequest(std::shared_ptr<HandlerEntry> entry, void *targetUserData)
            : RequestBase(Kind::Remove), entry(std::move(entry)), target(targetUserData)
        {
        }

        std::shared_ptr<HandlerEntry> entry;
        void *target;
    };

    struct WakeupRequest final : RequestBase {
        WakeupRequest() : RequestBase(Kind::Wakeup) {}
    };

    struct HandlerEntry {
        std::shared_ptr<EventHandler> handler;
        std::atomic<void *> currentRequest{nullptr};
        std::atomic<uint32_t> events{static_cast<uint32_t>(EventType::READ)};
        std::atomic<bool> active{true};
    };

    void Run();
    void DispatchEvent(const std::shared_ptr<HandlerEntry> &entry, int events);
    bool SubmitPollRequest(const std::shared_ptr<HandlerEntry> &entry, uint32_t events);
    bool SubmitPollRemove(const std::shared_ptr<HandlerEntry> &entry, void *targetUserData);
    bool SubmitWakeupWatch();
    void SubmitWakeupSignal();
    void Cleanup();

    io_uring *ring_ = nullptr;
    int wakeupFd_ = -1;

    std::atomic<bool> running_{false};
    std::atomic<bool> shuttingDown_{false};

    std::unique_ptr<std::thread> workerThread_;
    std::shared_mutex handlerMutex_;
    std::mutex signalMutex_;
    std::condition_variable runningSignal_;
    std::unordered_map<int, std::shared_ptr<HandlerEntry>> handlers_;

    std::mutex submitMutex_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_IO_URING_MANAGER_H
