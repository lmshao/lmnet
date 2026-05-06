/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "event_reactor.h"

#include <errno.h>
#include <pthread.h>
#include <sys/event.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstring>
#include <vector>

#include "internal_logger.h"

namespace {
constexpr int KQUEUE_EVENT_MAX = 1024;
constexpr intptr_t WAKE_EVENT_IDENT = 1;

int NormalizeEventsForKqueue(int events)
{
    constexpr int supportedEvents =
        static_cast<int>(lmshao::lmnet::EventType::READ) | static_cast<int>(lmshao::lmnet::EventType::WRITE);
    return events & supportedEvents;
}
} // namespace

namespace lmshao::lmnet {

EventReactor::EventReactor()
{
    reactorThread_ = std::make_unique<std::thread>([this]() { this->Run(); });
    std::unique_lock<std::mutex> lock(signalMutex_);
    runningSignal_.wait(lock, [this] { return startupComplete_; });
}

EventReactor::~EventReactor()
{
    LMNET_LOGD("enter");
    running_.store(false, std::memory_order_release);
    Wakeup();

    if (reactorThread_ && reactorThread_->joinable()) {
        reactorThread_->join();
    }

    if (kqueueFd_ != -1) {
        close(kqueueFd_);
        kqueueFd_ = -1;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    handlers_.clear();
    handlerEvents_.clear();
}

void EventReactor::Run()
{
    kqueueFd_ = kqueue();
    if (kqueueFd_ == -1) {
        LMNET_LOGE("kqueue create failed: %s", strerror(errno));
        {
            std::lock_guard<std::mutex> signalLock(signalMutex_);
            startupSucceeded_ = false;
            startupComplete_ = true;
        }
        runningSignal_.notify_all();
        return;
    }

    struct kevent change;
    EV_SET(&change, WAKE_EVENT_IDENT, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr) == -1) {
        LMNET_LOGE("kqueue register wake event failed: %s", strerror(errno));
        {
            std::lock_guard<std::mutex> signalLock(signalMutex_);
            startupSucceeded_ = false;
            startupComplete_ = true;
        }
        runningSignal_.notify_all();
        return;
    }

    std::string name = threadName_;
    if (name.size() > 63) {
        name = name.substr(0, 63);
    }
    if (!name.empty()) {
        pthread_setname_np(name.c_str());
    }

    {
        std::lock_guard<std::mutex> signalLock(signalMutex_);
        running_.store(true, std::memory_order_release);
        startupSucceeded_ = true;
        startupComplete_ = true;
    }
    runningSignal_.notify_all();

    std::vector<struct kevent> events(KQUEUE_EVENT_MAX);

    while (running_.load(std::memory_order_acquire)) {
        struct timespec timeout;
        timeout.tv_sec = 0;
        timeout.tv_nsec = 100 * 1000 * 1000; // 100ms

        int nfds = kevent(kqueueFd_, nullptr, 0, events.data(), static_cast<int>(events.size()), &timeout);
        if (nfds == -1) {
            if (errno == EINTR) {
                LMNET_LOGD("kqueue interrupted by signal");
                continue;
            }
            LMNET_LOGE("kevent wait failed: %s", strerror(errno));
            return;
        }

        for (int i = 0; i < nfds; ++i) {
            const struct kevent &kev = events[i];
            if (kev.filter == EVFILT_USER && kev.ident == WAKE_EVENT_IDENT) {
                continue;
            }

            std::shared_ptr<EventHandler> handler;
            {
                std::shared_lock<std::shared_mutex> lock(mutex_);
                auto it = handlers_.find(static_cast<int>(kev.ident));
                if (it != handlers_.end()) {
                    handler = it->second;
                }
            }

            if (handler) {
                try {
                    DispatchEvent(std::move(handler), kev);
                } catch (const std::exception &e) {
                    LMNET_LOGE("Exception in event handler for fd %lld: %s", static_cast<long long>(kev.ident),
                               e.what());
                } catch (...) {
                    LMNET_LOGE("Unknown exception in event handler for fd %lld", static_cast<long long>(kev.ident));
                }
            }
        }
    }
}

void EventReactor::DispatchEvent(std::shared_ptr<EventHandler> handler, const struct kevent &event)
{
    if (!handler) {
        return;
    }

    socket_t fd = static_cast<socket_t>(event.ident);

    if (event.flags & EV_ERROR) {
        LMNET_LOGE("kqueue event error on fd %d: %s", fd, strerror(static_cast<int>(event.data)));
        handler->HandleError(fd);
    }

    if ((event.flags & EV_ERROR) == 0 && event.filter == EVFILT_READ) {
        handler->HandleRead(fd);
    }

    if ((event.flags & EV_ERROR) == 0 && event.filter == EVFILT_WRITE) {
        handler->HandleWrite(fd);
    }

    if (event.flags & EV_EOF) {
        handler->HandleClose(fd);
    }
}

bool EventReactor::RegisterHandler(std::shared_ptr<EventHandler> handler)
{
    if (!handler) {
        LMNET_LOGE("Handler is nullptr");
        return false;
    }

    if (!running_.load(std::memory_order_acquire)) {
        LMNET_LOGE("Reactor not running");
        return false;
    }

    socket_t fd = handler->GetHandle();
    int events = NormalizeEventsForKqueue(handler->GetEvents());

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto [it, inserted] = handlers_.emplace(fd, handler);
        if (!inserted) {
            LMNET_LOGE("Handler already exists for fd:%d", fd);
            return false;
        }
        handlerEvents_[fd] = events;

        if (!ApplyEvents(fd, events, 0)) {
            handlers_.erase(it);
            handlerEvents_.erase(fd);
            return false;
        }
    }

    LMNET_LOGD("Handler registered for fd:%d", fd);
    return true;
}

bool EventReactor::RemoveHandler(socket_t fd)
{
    int oldEvents = 0;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = handlers_.find(fd);
        if (it == handlers_.end()) {
            LMNET_LOGW("Handler not found for fd:%d", fd);
            return false;
        }

        auto evIt = handlerEvents_.find(fd);
        if (evIt != handlerEvents_.end()) {
            oldEvents = evIt->second;
        }

        if (running_.load(std::memory_order_acquire) && !ApplyEvents(fd, 0, oldEvents)) {
            return false;
        }

        handlers_.erase(it);
        if (evIt != handlerEvents_.end()) {
            handlerEvents_.erase(evIt);
        }
    }

    return true;
}

bool EventReactor::ModifyHandler(socket_t fd, int events)
{
    const int normalizedEvents = NormalizeEventsForKqueue(events);

    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = handlerEvents_.find(fd);
        if (it == handlerEvents_.end()) {
            LMNET_LOGW("Handler not found for fd:%d during modify", fd);
            return false;
        }

        const int oldEvents = it->second;
        if (oldEvents == normalizedEvents) {
            return true;
        }

        if (!ApplyEvents(fd, normalizedEvents, oldEvents)) {
            return false;
        }

        it->second = normalizedEvents;
    }

    return true;
}

bool EventReactor::ApplyEvents(socket_t fd, int newEvents, int oldEvents)
{
    if (kqueueFd_ == -1) {
        LMNET_LOGE("kqueue fd invalid");
        return false;
    }

    std::vector<struct kevent> changes;
    changes.reserve(4);

    auto addChange = [&changes, fd](int filter, int flags) {
        struct kevent change;
        EV_SET(&change, fd, filter, flags, 0, 0, nullptr);
        changes.push_back(change);
    };

    auto wasEnabled = [&oldEvents](EventType type) { return (oldEvents & static_cast<int>(type)) != 0; };

    auto shouldEnable = [&newEvents](EventType type) { return (newEvents & static_cast<int>(type)) != 0; };

    auto syncFilter = [&](EventType type, int filter) {
        bool had = wasEnabled(type);
        bool want = shouldEnable(type);
        if (had && !want) {
            addChange(filter, EV_DELETE);
        } else if (!had && want) {
            addChange(filter, EV_ADD | EV_CLEAR | EV_ENABLE);
        } else if (had && want) {
            addChange(filter, EV_ADD | EV_CLEAR | EV_ENABLE);
        }
    };

    syncFilter(EventType::READ, EVFILT_READ);
    syncFilter(EventType::WRITE, EVFILT_WRITE);

    if (changes.empty()) {
        return true;
    }

    if (kevent(kqueueFd_, changes.data(), static_cast<int>(changes.size()), nullptr, 0, nullptr) == -1) {
        if (newEvents == 0 && (errno == ENOENT || errno == EBADF)) {
            LMNET_LOGW("Ignore kevent delete race for fd %d: %s", fd, strerror(errno));
            return true;
        }
        LMNET_LOGE("kevent apply events failed for fd %d: %s", fd, strerror(errno));
        return false;
    }

    return true;
}

void EventReactor::Wakeup()
{
    if (kqueueFd_ == -1) {
        return;
    }

    struct kevent change;
    EV_SET(&change, WAKE_EVENT_IDENT, EVFILT_USER, 0, NOTE_TRIGGER, 0, nullptr);
    if (kevent(kqueueFd_, &change, 1, nullptr, 0, nullptr) == -1) {
        LMNET_LOGE("kqueue wakeup failed: %s", strerror(errno));
    }
}

void EventReactor::SetThreadName(const std::string &name)
{
    if (name.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(signalMutex_);
        threadName_ = name;
    }

    Wakeup();
}

} // namespace lmshao::lmnet
