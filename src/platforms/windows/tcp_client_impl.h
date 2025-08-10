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
#include <thread>

#include "common.h" // provides winsock2.h on Windows
#include "itcp_client.h"
// After winsock2.h is available we can include mswsock for LPFN_CONNECTEX
#include <mswsock.h>

#include "data_buffer.h"
#include "iclient_listener.h"
#include "iocp_base.h"

namespace lmshao::network {

class TcpClientImpl final : public ITcpClient,
                            public std::enable_shared_from_this<TcpClientImpl>,
                            public Creatable<TcpClientImpl> {
    friend class Creatable<TcpClientImpl>;

public:
    TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);
    ~TcpClientImpl() override = default;

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override;
    bool Connect() override;
    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override;
    socket_t GetSocketFd() const override;

private:
    TcpClientImpl() = default;
    void PostRecv();
    void PostSend(const void *data, size_t len);
    void StartLoop();
    void WorkerLoop();
    bool LoadConnectEx();

    std::string remoteIp_;
    uint16_t remotePort_{0};
    std::string localIp_;
    uint16_t localPort_{0};
    SOCKET socket_{INVALID_SOCKET};
    HANDLE iocp_{nullptr};
    bool running_{false};
    std::thread worker_;
    LPFN_CONNECTEX fnConnectEx{nullptr};
    std::shared_ptr<IClientListener> listener_;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_TCP_CLIENT_IMPL_H
