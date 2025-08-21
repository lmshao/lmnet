/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_SESSION_H
#define LMSHAO_NETWORK_SESSION_H

#include <coreutils/data_buffer.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "common.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

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

#endif // LMSHAO_NETWORK_SESSION_H