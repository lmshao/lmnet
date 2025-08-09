/**
 * @file iunix_server.h
 * @brief Unix Server Interface Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_I_UNIX_SERVER_H
#define NETWORK_I_UNIX_SERVER_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iserver_listener.h"

namespace lmshao::network {

class IUnixServer {
public:
    virtual ~IUnixServer() = default;

    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IServerListener> listener) = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::network

#endif // NETWORK_I_UNIX_SERVER_H
