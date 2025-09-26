/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "io_uring_manager.h"

#include <liburing.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

#include "internal_logger.h"

namespace {
constexpr uint32_t kDefaultRingEntries = 512;
constexpr uint32_t kWakeupPollMask = POLLIN;
constexpr int kThreadNameMaxLen = 15;

inline uint32_t ToPollMask(int events)
{
    uint32_t mask = 0;
    if (events & static_cast<int>(lmshao::lmnet::EventType::READ)) {
        mask |= POLLIN;
    }
    if (events & static_cast<int>(lmshao::lmnet::EventType::WRITE)) {
        mask |= POLLOUT;
    }
    if (events & static_cast<int>(lmshao::lmnet::EventType::ERROR)) {
        mask |= POLLERR;
    }
    if (events & static_cast<int>(lmshao::lmnet::EventType::CLOSE)) {
        mask |= POLLHUP | POLLRDHUP;
    }
    if (mask == 0) {
        mask = POLLIN;
    }
    return mask;
}

inline int ToEventMask(int pollMask)
{
    int events = 0;
    if (pollMask & (POLLERR)) {
        events |= static_cast<int>(lmshao::lmnet::EventType::ERROR);
    }
    if (pollMask & (POLLHUP | POLLRDHUP)) {
        events |= static_cast<int>(lmshao::lmnet::EventType::CLOSE);
    }
    if (pollMask & POLLIN) {
        events |= static_cast<int>(lmshao::lmnet::EventType::READ);
    }
    if (pollMask & POLLOUT) {
        events |= static_cast<int>(lmshao::lmnet::EventType::WRITE);
    }
    return events;
}

inline void ReadEventFd(int fd)
{
    if (fd < 0) {
        return;
    }
    uint64_t value;
    while (read(fd, &value, sizeof(value)) == -1 && errno == EINTR) {
    }
}

} // namespace

namespace lmshao::lmnet {

IoUringManager::IoUringManager()
{
    ring_ = new io_uring;
    const int initResult = io_uring_queue_init(kDefaultRingEntries, ring_, 0);
    if (initResult < 0) {
        LMNET_LOGE("io_uring_queue_init failed: %s (%d)", strerror(-initResult), initResult);
        delete ring_;
        ring_ = nullptr;
        return;
    }

    wakeupFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeupFd_ == -1) {
        LMNET_LOGE("eventfd creation failed: %s", strerror(errno));
        Cleanup();
        return;
    }

    workerThread_ = std::make_unique<std::thread>([this]() { Run(); });

    std::unique_lock<std::mutex> lock(signalMutex_);
    runningSignal_.wait_for(lock, std::chrono::milliseconds(50), [this] { return running_.load() || ring_ == nullptr; });
}

IoUringManager::~IoUringManager()
{
    LMNET_LOGD("IoUringManager shutdown begin");

    shuttingDown_.store(true);

    std::vector<std::shared_ptr<HandlerEntry>> pendingHandlers;
    {
        std::unique_lock<std::shared_mutex> lock(handlerMutex_);
        pendingHandlers.reserve(handlers_.size());
        for (auto &pair : handlers_) {
            if (pair.second) {
                pair.second->active.store(false);
                pendingHandlers.push_back(pair.second);
            }
        }
        handlers_.clear();
    }

    for (auto &entry : pendingHandlers) {
        if (!entry) {
            continue;
        }
        void *token = entry->currentRequest.load();
        if (token) {
            SubmitPollRemove(entry, token);
        }
    }

    running_.store(false);
    SubmitWakeupSignal();

    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }

    Cleanup();
    LMNET_LOGD("IoUringManager shutdown finished");
}

void IoUringManager::SetThreadName(const std::string &name)
{
    if (workerThread_ && !name.empty()) {
        std::string threadName = name;
        if (threadName.size() > kThreadNameMaxLen) {
            threadName.resize(kThreadNameMaxLen);
        }
        pthread_setname_np(workerThread_->native_handle(), threadName.c_str());
    }
}

bool IoUringManager::RegisterHandler(std::shared_ptr<EventHandler> handler)
{
    if (!handler) {
        LMNET_LOGE("RegisterHandler: handler is null");
        return false;
    }

    if (!ring_) {
        LMNET_LOGE("RegisterHandler: io_uring not initialized");
        return false;
    }

    if (!running_.load()) {
        LMNET_LOGE("RegisterHandler: manager is not running");
        return false;
    }

    socket_t fd = handler->GetHandle();
    auto entry = std::make_shared<HandlerEntry>();
    entry->handler = std::move(handler);
    entry->events.store(ToPollMask(entry->handler->GetEvents()));

    {
        std::unique_lock<std::shared_mutex> lock(handlerMutex_);
        if (handlers_.find(fd) != handlers_.end()) {
            LMNET_LOGW("RegisterHandler: handler already exists for fd %d", fd);
            return false;
        }
        handlers_.emplace(fd, entry);
    }

    const uint32_t pollMask = entry->events.load();
    if (!SubmitPollRequest(entry, pollMask)) {
        LMNET_LOGE("RegisterHandler: failed to submit poll request for fd %d", fd);
        entry->active.store(false);
        std::unique_lock<std::shared_mutex> lock(handlerMutex_);
        handlers_.erase(fd);
        return false;
    }

    LMNET_LOGD("RegisterHandler success fd=%d mask=0x%x", fd, pollMask);
    return true;
}

bool IoUringManager::RemoveHandler(socket_t fd)
{
    std::shared_ptr<HandlerEntry> entry;
    {
        std::unique_lock<std::shared_mutex> lock(handlerMutex_);
        auto it = handlers_.find(fd);
        if (it == handlers_.end()) {
            LMNET_LOGW("RemoveHandler: handler not found for fd %d", fd);
            return false;
        }
        entry = it->second;
        handlers_.erase(it);
    }

    if (!entry) {
        return true;
    }

    entry->active.store(false);
    void *token = entry->currentRequest.load();
    if (token) {
        SubmitPollRemove(entry, token);
    }

    LMNET_LOGD("RemoveHandler success fd=%d", fd);
    return true;
}

bool IoUringManager::ModifyHandler(socket_t fd, int events)
{
    std::shared_ptr<HandlerEntry> entry;
    {
        std::shared_lock<std::shared_mutex> lock(handlerMutex_);
        auto it = handlers_.find(fd);
        if (it == handlers_.end()) {
            LMNET_LOGW("ModifyHandler: handler not found for fd %d", fd);
            return false;
        }
        entry = it->second;
    }

    if (!entry || !entry->active.load()) {
        return false;
    }

    const uint32_t newMask = ToPollMask(events);
    entry->events.store(newMask);

    void *token = entry->currentRequest.load();
    if (!token) {
        return SubmitPollRequest(entry, newMask);
    }

    return SubmitPollRemove(entry, token);
}

void IoUringManager::Run()
{
    if (!ring_ || wakeupFd_ == -1) {
        return;
    }

    running_.store(true);
    SubmitWakeupWatch();
    runningSignal_.notify_all();

    while (running_.load() || shuttingDown_.load()) {
        io_uring_cqe *cqe = nullptr;
        int waitResult = io_uring_wait_cqe(ring_, &cqe);
        if (waitResult < 0) {
            if (waitResult == -EINTR) {
                continue;
            }
            if (shuttingDown_.load()) {
                break;
            }
            LMNET_LOGE("io_uring_wait_cqe failed: %s (%d)", strerror(-waitResult), waitResult);
            continue;
        }

        RequestBase *base = static_cast<RequestBase *>(io_uring_cqe_get_data(cqe));
        int res = cqe->res;
        io_uring_cqe_seen(ring_, cqe);

        if (!base) {
            continue;
        }

        switch (base->kind) {
        case RequestBase::Kind::Poll: {
            auto *pollReq = static_cast<PollRequest *>(base);
            auto entry = pollReq->entry;
            if (entry) {
                entry->currentRequest.store(nullptr);
                if (entry->active.load()) {
                    if (entry->handler && entry->handler->GetHandle() != pollReq->fd) {
                        LMNET_LOGW("Poll result fd mismatch: expected %d actual %d", entry->handler->GetHandle(), pollReq->fd);
                    }
                    if (res >= 0) {
                        int eventMask = ToEventMask(res);
                        if (eventMask != 0) {
                            DispatchEvent(entry, eventMask);
                        }
                    } else {
                        int eventMask = static_cast<int>(EventType::ERROR);
                        if (res == -ECANCELED) {
                            eventMask = 0; // cancellation for modify/remove, no dispatch
                        }
                        if (eventMask != 0) {
                            DispatchEvent(entry, eventMask);
                        }
                    }

                    if (entry->active.load()) {
                        const uint32_t mask = entry->events.load();
                        SubmitPollRequest(entry, mask);
                    }
                }
            }
            delete pollReq;
            break;
        }
        case RequestBase::Kind::Remove: {
            auto *removeReq = static_cast<RemoveRequest *>(base);
            delete removeReq;
            break;
        }
        case RequestBase::Kind::Wakeup: {
            auto *wakeupReq = static_cast<WakeupRequest *>(base);
            ReadEventFd(wakeupFd_);
            delete wakeupReq;
            if (!shuttingDown_.load()) {
                SubmitWakeupWatch();
            }
            break;
        }
        }

        if (!running_.load() && shuttingDown_.load()) {
            break;
        }
    }

    running_.store(false);
}

void IoUringManager::DispatchEvent(const std::shared_ptr<HandlerEntry> &entry, int events)
{
    if (!entry || !entry->handler) {
        return;
    }

    socket_t fd = entry->handler->GetHandle();
    if (events & static_cast<int>(EventType::ERROR)) {
        entry->handler->HandleError(fd);
    }
    if (events & static_cast<int>(EventType::CLOSE)) {
        entry->handler->HandleClose(fd);
    }
    if (events & static_cast<int>(EventType::READ)) {
        entry->handler->HandleRead(fd);
    }
    if (events & static_cast<int>(EventType::WRITE)) {
        entry->handler->HandleWrite(fd);
    }
}

bool IoUringManager::SubmitPollRequest(const std::shared_ptr<HandlerEntry> &entry, uint32_t events)
{
    if (!entry || !ring_) {
        return false;
    }

    auto *req = new PollRequest(entry, entry->handler->GetHandle(), events);
    entry->currentRequest.store(req);

    std::scoped_lock lock(submitMutex_);
    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        int submitRet = io_uring_submit(ring_);
        if (submitRet < 0) {
            LMNET_LOGE("SubmitPollRequest: io_uring_submit failed %s (%d)", strerror(-submitRet), submitRet);
            entry->currentRequest.store(nullptr);
            delete req;
            return false;
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            LMNET_LOGE("SubmitPollRequest: no SQE available");
            entry->currentRequest.store(nullptr);
            delete req;
            return false;
        }
    }

    io_uring_prep_poll_add(sqe, req->fd, events);
    io_uring_sqe_set_data(sqe, req);
    int submit = io_uring_submit(ring_);
    if (submit < 0) {
        LMNET_LOGE("SubmitPollRequest: submit failed %s (%d)", strerror(-submit), submit);
        entry->currentRequest.store(nullptr);
        delete req;
        return false;
    }

    return true;
}

bool IoUringManager::SubmitPollRemove(const std::shared_ptr<HandlerEntry> &entry, void *targetUserData)
{
    if (!entry || !targetUserData || !ring_) {
        return true;
    }

    auto *req = new RemoveRequest(entry, targetUserData);

    std::scoped_lock lock(submitMutex_);
    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        int submitRet = io_uring_submit(ring_);
        if (submitRet < 0) {
            LMNET_LOGE("SubmitPollRemove: submit failed %s (%d)", strerror(-submitRet), submitRet);
            delete req;
            return false;
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            LMNET_LOGE("SubmitPollRemove: no SQE available");
            delete req;
            return false;
        }
    }

    io_uring_prep_poll_remove(sqe, reinterpret_cast<__u64>(targetUserData));
    io_uring_sqe_set_data(sqe, req);
    int submit = io_uring_submit(ring_);
    if (submit < 0) {
        LMNET_LOGE("SubmitPollRemove: submit failed %s (%d)", strerror(-submit), submit);
        delete req;
        return false;
    }

    return true;
}

bool IoUringManager::SubmitWakeupWatch()
{
    if (wakeupFd_ < 0 || !ring_) {
        return false;
    }

    auto *req = new WakeupRequest();

    std::scoped_lock lock(submitMutex_);
    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
        int submitRet = io_uring_submit(ring_);
        if (submitRet < 0) {
            delete req;
            return false;
        }
        sqe = io_uring_get_sqe(ring_);
        if (!sqe) {
            delete req;
            return false;
        }
    }

    io_uring_prep_poll_add(sqe, wakeupFd_, kWakeupPollMask);
    io_uring_sqe_set_data(sqe, req);
    int submit = io_uring_submit(ring_);
    if (submit < 0) {
        delete req;
        return false;
    }

    return true;
}

void IoUringManager::SubmitWakeupSignal()
{
    if (wakeupFd_ < 0) {
        return;
    }

    uint64_t one = 1;
    if (write(wakeupFd_, &one, sizeof(one)) < 0) {
        if (errno != EAGAIN && errno != EINTR) {
            LMNET_LOGE("SubmitWakeupSignal: write failed %s", strerror(errno));
        }
    }
}

void IoUringManager::Cleanup()
{
    if (ring_) {
        io_uring_queue_exit(ring_);
        delete ring_;
        ring_ = nullptr;
    }

    if (wakeupFd_ != -1) {
        close(wakeupFd_);
        wakeupFd_ = -1;
    }
}

} // namespace lmshao::lmnet
