/**
 * UDP Server Windows Implementation Header
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_WINDOWS_UDP_SERVER_IMPL_H
#define LMSHAO_NETWORK_WINDOWS_UDP_SERVER_IMPL_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "coreutils/data_buffer.h"
#include "iocp_manager.h"
#include "iserver_listener.h"
#include "network/common.h"

namespace lmshao::network {

class UdpServerImpl final : public BaseServer,
                            public std::enable_shared_from_this<UdpServerImpl>,
                            public Creatable<UdpServerImpl>,
                            public IIocpHandler {
    friend class Creatable<UdpServerImpl>;

public:
    ~UdpServerImpl();

    // IUdpServer interface
    bool Init() override;  // create socket & bind
    bool Start() override; // start IOCP worker & post recvs
    bool Stop() override;  // stop worker
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Send(socket_t fd, std::string ip, uint16_t port, const void *data, size_t len) override; // synchronous sendto
    bool Send(socket_t fd, std::string ip, uint16_t port, const std::string &str) override;
    bool Send(socket_t fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) override;
    socket_t GetSocketFd() const override { return socket_; }
    bool SendTo(const sockaddr_in &to, const void *data, size_t len); // Helper for session replies

    // IIocpHandler interface
    void OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error) override;

protected:
    UdpServerImpl(std::string ip, uint16_t port);

private:
    UdpServerImpl() = default;

    // Internal helpers
    void PostRecv();
    void HandlePacket(const char *data, size_t len, const sockaddr_in &from);
    void CloseSocket();

    friend class UdpSessionWin; // Allow session to access SendTo

    // State
    std::string ip_;
    uint16_t port_{0};
    socket_t socket_{INVALID_SOCKET};
    std::shared_ptr<IServerListener> listener_;
    bool running_{false};
    sockaddr_in ctxLastFrom_{}; // last peer for error/close callbacks
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_WINDOWS_UDP_SERVER_IMPL_H
