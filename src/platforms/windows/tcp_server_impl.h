/**
 * TCP Server Windows Implementation Header
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_WINDOWS_TCP_SERVER_IMPL_H
#define LMSHAO_NETWORK_WINDOWS_TCP_SERVER_IMPL_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "base_server.h"
#include "coreutils/data_buffer.h"
#include "iocp_manager.h"
#include "iserver_listener.h"
#include "network/common.h"

namespace lmshao::network {

// Forward declare the per-I/O context at namespace scope (defined in cpp)
struct PerIoContextTCP;

class TcpServerImpl final : public BaseServer,
                            public std::enable_shared_from_this<TcpServerImpl>,
                            public Creatable<TcpServerImpl>,
                            public IIocpHandler {
    friend class Creatable<TcpServerImpl>;

public:
    TcpServerImpl(std::string listenIp, uint16_t listenPort);
    explicit TcpServerImpl(uint16_t listenPort);
    ~TcpServerImpl() override = default;

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override;
    bool Start() override;
    bool Stop() override;
    bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size) override;
    bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) override;
    bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str) override;
    socket_t GetSocketFd() const override;

    // IIocpHandler interface
    void OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error) override;

private:
    TcpServerImpl() = default;
    void PostAccept();
    void PostRecv(std::shared_ptr<class SessionImpl> session);
    void HandleAccept(PerIoContextTCP *ctx, DWORD bytes);
    void HandleRecv(PerIoContextTCP *ctx, DWORD bytes);

    std::string ip_;
    uint16_t port_{0};
    std::shared_ptr<class TcpServerState> state_;
    std::shared_ptr<IServerListener> listener_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_WINDOWS_TCP_SERVER_IMPL_H
