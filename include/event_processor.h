//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_EVENT_PROCESSOR_H
#define NETWORK_EVENT_PROCESSOR_H

#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "event_reactor.h"
#include "singleton.h"

class EventProcessor : public Singleton<EventProcessor> {
    friend class Singleton<EventProcessor>;

public:
    ~EventProcessor() override;
    // for tcp server
    void AddServiceFd(int fd, std::function<void(int)> callback);
    void RemoveServiceFd(int fd);

    void AddConnectionFd(int fd, std::function<void(int)> callback);
    void RemoveConnectionFd(int fd);

protected:
    EventProcessor();

private:
    std::mutex callbackMutex_;
    std::mutex subReactorsMutex_;

    // for TCP server socket acceptor
    std::unique_ptr<EventReactor> mainReactor_;
    std::map<std::shared_ptr<EventReactor>, std::unordered_set<int>> subReactorsMap_;
    std::unordered_map<int, std::function<void(int)>> fdCallbacks_;
};

#endif // NETWORK_EVENT_PROCESSOR_H