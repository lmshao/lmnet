//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_ICLIENT_LISTENER_H
#define NETWORK_ICLIENT_LISTENER_H

#include <memory>
#include "data_buffer.h"

class IClientListener {
public:
    virtual ~IClientListener() = default;
    virtual void OnReceive(std::shared_ptr<DataBuffer> buffer) = 0;
    virtual void OnClose() = 0;
    virtual void OnError(const std::string &errorInfo) = 0;
};

#endif // NETWORK_ICLIENT_LISTENER_H