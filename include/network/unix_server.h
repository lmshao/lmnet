/**
 * @file unix_server.h
 * @brief Unix Domain Socket Server Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_UNIX_SERVER_H
#define NETWORK_UNIX_SERVER_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <memory>
#include <string>

#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class IUnixServer;

class UnixServer {
public:
    static std::shared_ptr<UnixServer> Create(const std::string &socketPath)
    {
        return std::shared_ptr<UnixServer>(new UnixServer(socketPath));
    }

    ~UnixServer();

    bool Init();
    void SetListener(std::shared_ptr<IServerListener> listener);
    bool Start();
    bool Stop();
    socket_t GetSocketFd() const;

protected:
    explicit UnixServer(const std::string &socketPath);

private:
    std::shared_ptr<IUnixServer> impl_;
};

} // namespace lmshao::network

#endif // NETWORK_UNIX_SERVER_H
