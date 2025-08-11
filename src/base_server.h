/**
 * @file base_server.h
 * @brief Base Server Interface
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_BASE_SERVER_H
#define NETWORK_BASE_SERVER_H

#include <memory>

#include "common.h"
#include "data_buffer.h"

namespace lmshao::network {

class IServerListener;
class BaseServer {
public:
    virtual ~BaseServer() = default;
    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IServerListener> listener) = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size) = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str) = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::network

#endif // NETWORK_BASE_SERVER_H