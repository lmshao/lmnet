/**
 * @file tcp_server_impl.h
 * @brief TCP Server Windows Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_WINDOWS_TCP_SERVER_IMPL_H
#define NETWORK_WINDOWS_TCP_SERVER_IMPL_H

#include <cstdint>
#include <memory>
#include <string>

#include "../../itcp_server.h"
#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class TcpServerImpl final : public ITcpServer,
                            public std::enable_shared_from_this<TcpServerImpl>,
                            public Creatable<TcpServerImpl> {
    friend class Creatable<TcpServerImpl>;

public:
    ~TcpServerImpl() = default;

    // ITcpServer interface - empty implementations
    bool Init() override { return false; }
    void SetListener(std::shared_ptr<IServerListener> listener) override {}
    bool Start() override { return false; }
    bool Stop() override { return false; }
    socket_t GetSocketFd() const override { return -1; }

    TcpServerImpl(std::string listenIp, uint16_t listenPort) {}
    explicit TcpServerImpl(uint16_t listenPort) {}

private:
    TcpServerImpl() = default;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_TCP_SERVER_IMPL_H
