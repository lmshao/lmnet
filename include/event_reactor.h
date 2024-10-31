//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_EVENT_REACTOR_H
#define NETWORK_EVENT_REACTOR_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "singleton.h"

class EventReactor : public Singleton<EventReactor> {
public:
    friend class Singleton<EventReactor>;

    EventReactor();
    ~EventReactor();

    void AddDescriptor(int fd, std::function<void(int)> callback);
    void RemoveDescriptor(int fd);

    void SetThreadName(std::string name);

private:
    void Run();

private:
    int epollFd_ = -1;
    bool running_ = false;

    std::mutex mutex_;
    std::mutex signalMutex_;
    std::condition_variable runningSignal_;
    std::unordered_map<int, std::function<void(int)>> fds_;
    std::unique_ptr<std::thread> epollThread_;
};
#endif // NETWORK_EVENT_REACTOR_H