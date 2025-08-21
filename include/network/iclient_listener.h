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

#include "common.h"
#include "coreutils/data_buffer.h"

namespace lmshao::network {

using namespace lmshao::coreutils;

class IClientListener {
public:
    virtual ~IClientListener() = default;
    virtual void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> buffer) = 0;
    virtual void OnClose(socket_t fd) = 0;
    virtual void OnError(socket_t fd, const std::string &errorInfo) = 0;
};

} // namespace lmshao::network

#endif // NETWORK_ICLIENT_LISTENER_H