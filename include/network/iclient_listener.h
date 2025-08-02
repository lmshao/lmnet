/**
 * @file iclient_listener.h
 * @brief Client Listener Interface
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

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