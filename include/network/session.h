/**
 * @file session.h
 * @brief Session Management Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_SESSION_H
#define NETWORK_SESSION_H

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "common.h"
#include "data_buffer.h"

namespace lmshao::network {

class Session {
public:
    std::string host;
    uint16_t port;
    socket_t fd = INVALID_SOCKET;

    virtual bool Send(std::shared_ptr<DataBuffer> buffer) const = 0;
    virtual bool Send(const std::string &str) const = 0;
    virtual bool Send(const void *data, size_t size) const = 0;
    virtual std::string ClientInfo() const = 0;

protected:
    Session() = default;
    virtual ~Session() = default;
};

} // namespace lmshao::network

#endif // NETWORK_SESSION_H