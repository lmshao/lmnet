/**
 * @file tcp_client_impl.h
 * @brief TCP Client Windows Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_WINDOWS_TCP_CLIENT_IMPL_H
#define NETWORK_WINDOWS_TCP_CLIENT_IMPL_H

#include <cstdint>
#include <memory>
#include <string>

#include "../../itcp_client.h"
#include "common.h"
#include "data_buffer.h"
#include "iclient_listener.h"

namespace lmshao::network {

class TcpClientImpl final : public ITcpClient,
                            public std::enable_shared_from_this<TcpClientImpl>,
                            public Creatable<TcpClientImpl> {
    friend class Creatable<TcpClientImpl>;

public:
    ~TcpClientImpl() = default;

    // ITcpClient interface - empty implementations
    bool Init() override { return false; }
    void SetListener(std::shared_ptr<IClientListener> listener) override {}
    bool Connect() override { return false; }
    bool Send(const std::string &str) override { return false; }
    bool Send(const void *data, size_t len) override { return false; }
    bool Send(std::shared_ptr<DataBuffer> data) override { return false; }
    void Close() override {}
    socket_t GetSocketFd() const override { return -1; }

private:
    TcpClientImpl() = default;
    TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0)
        : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
    {
    }

    std::string remoteIp_;
    uint16_t remotePort_;
    std::string localIp_;
    uint16_t localPort_;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_TCP_CLIENT_IMPL_H
