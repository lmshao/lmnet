/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_I_UNIX_CLIENT_H
#define LMSHAO_NETWORK_I_UNIX_CLIENT_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <coreutils/data_buffer.h>

#include <cstdint>
#include <memory>
#include <string>

#include "network/common.h"
#include "network/iclient_listener.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

class IUnixClient {
public:
    virtual ~IUnixClient() = default;

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

#endif // LMSHAO_NETWORK_I_UNIX_CLIENT_H
