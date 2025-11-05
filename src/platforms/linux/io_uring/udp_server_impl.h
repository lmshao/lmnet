/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H

#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"

namespace lmshao::lmnet {

class UdpServerImpl final : public BaseServer, public std::enable_shared_from_this<UdpServerImpl> {

public:
    UdpServerImpl(std::string ip, uint16_t port);
    ~UdpServerImpl();

    // impl IUdpServer
    bool Init() override;
    bool Start() override;
    bool Stop() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }

    socket_t GetSocketFd() const override { return ipv4_socket_ != INVALID_SOCKET ? ipv4_socket_ : ipv6_socket_; }

private:
    void StartReceiveOnSocket(socket_t fd);
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_storage &from_addr,
                       socklen_t addrlen);

private:
    std::string ip_;
    uint16_t port_;

    socket_t ipv4_socket_ = INVALID_SOCKET;
    socket_t ipv6_socket_ = INVALID_SOCKET;

    std::weak_ptr<IServerListener> listener_;
    std::atomic_bool isRunning_{false};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
