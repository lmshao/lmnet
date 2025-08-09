/**
 * @file iudp_server.h
 * @brief UDP Server Interface Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_I_UDP_SERVER_H
#define NETWORK_I_UDP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iserver_listener.h"

namespace lmshao::network {

class IUdpServer {
public:
    virtual ~IUdpServer() = default;

    virtual bool Init() = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual void SetListener(std::shared_ptr<IServerListener> listener) = 0;
    virtual bool Send(int fd, std::string ip, uint16_t port, const void *data, size_t len) = 0;
    virtual bool Send(int fd, std::string ip, uint16_t port, const std::string &str) = 0;
    virtual bool Send(int fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::network

#endif // NETWORK_I_UDP_SERVER_H
