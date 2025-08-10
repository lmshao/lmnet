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
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "common.h"
#include "data_buffer.h"
#include "iocp_manager.h"
#include "iserver_listener.h"
#include "itcp_server.h"

namespace lmshao::network {

// Forward declare the per-I/O context at namespace scope (defined in cpp)
struct PerIoContextTCP;

class TcpServerImpl final : public ITcpServer,
                            public std::enable_shared_from_this<TcpServerImpl>,
                            public Creatable<TcpServerImpl>,
                            public win::IIocpHandler {
    friend class Creatable<TcpServerImpl>;

public:
    TcpServerImpl(std::string listenIp, uint16_t listenPort);
    explicit TcpServerImpl(uint16_t listenPort);
    ~TcpServerImpl() override = default;

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override;
    bool Start() override;
    bool Stop() override;
    socket_t GetSocketFd() const override;

    // IIocpHandler interface
    void OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error) override;

private:
    TcpServerImpl() = default;
    void PostAccept();
    void PostRecv(std::shared_ptr<class TcpSessionWin> session);
    void HandleAccept(PerIoContextTCP *ctx, DWORD bytes);
    void HandleRecv(PerIoContextTCP *ctx, DWORD bytes);

    std::string ip_;
    uint16_t port_{0};
    std::shared_ptr<class TcpServerState> state_;
    std::shared_ptr<IServerListener> listener_;
};

} // namespace lmshao::network

#endif // NETWORK_WINDOWS_TCP_SERVER_IMPL_H
