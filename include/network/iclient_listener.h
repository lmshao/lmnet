//
// Copyright © 2024-2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_ICLIENT_LISTENER_H
#define NETWORK_ICLIENT_LISTENER_H

#include <memory>

#include "data_buffer.h"

namespace lmshao::network {

class IClientListener {
public:
    virtual ~IClientListener() = default;
    virtual void OnReceive(int fd, std::shared_ptr<DataBuffer> buffer) = 0;
    virtual void OnClose(int fd) = 0;
    virtual void OnError(int fd, const std::string &errorInfo) = 0;
};

} // namespace lmshao::network

#endif // NETWORK_ICLIENT_LISTENER_H