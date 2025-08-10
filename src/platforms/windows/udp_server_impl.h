/**
 * @file udp_server_impl.h
 * @brief UDP Server Windows Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_WINDOWS_UDP_SERVER_IMPL_H
#define NETWORK_WINDOWS_UDP_SERVER_IMPL_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iserver_listener.h"
#include "iudp_server.h"

namespace lmshao::network {

class UdpServerImpl final : public IUdpServer,
                            public std::enable_shared_from_this<UdpServerImpl>,
                            public Creatable<UdpServerImpl> {
    friend class Creatable<UdpServerImpl>;

public:
    ~UdpServerImpl() = default;

    // IUdpServer interface - empty implementations
    bool Init() override { return false; }
    bool Start() override { return false; }
    bool Stop() override { return false; }
    void SetListener(std::shared_ptr<IServerListener> listener) override {}
    bool Send(int fd, std::string ip, uint16_t port, const void *data, size_t len) override { return false; }
    bool Send(int fd, std::string ip, uint16_t port, const std::string &str) override { return false; }
    bool Send(int fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) override { return false; }
    socket_t GetSocketFd() const override { return INVALID_SOCKET; }

protected:
    // Constructor should be protected in IMPL pattern
    UdpServerImpl(std::string ip, uint16_t port) {}

private:
    UdpServerImpl() = default;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_UDP_SERVER_IMPL_H
