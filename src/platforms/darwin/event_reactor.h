/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_DARWIN_EVENT_REACTOR_H
#define LMSHAO_LMNET_DARWIN_EVENT_REACTOR_H

#include <lmcore/singleton.h>

#include <condition_variable>
#include <memory>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>

struct kevent;

#include "lmnet/common.h"

namespace lmshao::lmnet {

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

class EventReactor : public lmcore::Singleton<EventReactor> {
public:
    friend class lmcore::Singleton<EventReactor>;

    EventReactor();
    ~EventReactor();

    bool RegisterHandler(std::shared_ptr<EventHandler> handler);
    bool RemoveHandler(socket_t fd);
    bool ModifyHandler(socket_t fd, int events);

    void SetThreadName(const std::string &name);

private:
    void Run();
    void DispatchEvent(const struct kevent &event);
    bool ApplyEvents(socket_t fd, int newEvents, int oldEvents);
    void Wakeup();

private:
    int kqueueFd_ = -1;
    bool running_ = false;

    std::shared_mutex mutex_;
    std::mutex signalMutex_;
    std::condition_variable runningSignal_;

    std::unordered_map<int, std::shared_ptr<EventHandler>> handlers_;
    std::unordered_map<int, int> handlerEvents_;

    std::unique_ptr<std::thread> reactorThread_;
    std::string threadName_ = "EventReactor";
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_DARWIN_EVENT_REACTOR_H
