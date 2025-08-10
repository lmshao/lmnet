/**
 * @file udp_client_impl.h
 * @brief UDP Client Windows Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_WINDOWS_UDP_CLIENT_IMPL_H
#define NETWORK_WINDOWS_UDP_CLIENT_IMPL_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iclient_listener.h"
#include "iudp_client.h"

namespace lmshao::network {

class UdpClientImpl final : public IUdpClient,
                            public std::enable_shared_from_this<UdpClientImpl>,
                            public Creatable<UdpClientImpl> {
    friend class Creatable<UdpClientImpl>;

public:
    ~UdpClientImpl() = default;

    // IUdpClient interface - empty implementations
    bool Init() override { return false; }
    void SetListener(std::shared_ptr<IClientListener> listener) override {}
    bool Send(const std::string &str) override { return false; }
    bool Send(const void *data, size_t len) override { return false; }
    bool Send(std::shared_ptr<DataBuffer> data) override { return false; }
    void Close() override {}
    socket_t GetSocketFd() const override { return INVALID_SOCKET; }

    UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0) {}

private:
    UdpClientImpl() = default;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_UDP_CLIENT_IMPL_H
