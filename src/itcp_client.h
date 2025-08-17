/**
 * @file itcp_client.h
 * @brief TCP Client Interface Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_I_TCP_CLIENT_H
#define NETWORK_I_TCP_CLIENT_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "core-utils/data_buffer.h"
#include "iclient_listener.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

class ITcpClient {
public:
    virtual ~ITcpClient() = default;

    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IClientListener> listener) = 0;
    virtual bool Connect() = 0;
    virtual bool Send(const std::string &str) = 0;
    virtual bool Send(const void *data, size_t len) = 0;
    virtual bool Send(std::shared_ptr<DataBuffer> data) = 0;
    virtual void Close() = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::network

#endif // NETWORK_I_TCP_CLIENT_H