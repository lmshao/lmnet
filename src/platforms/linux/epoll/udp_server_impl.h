/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H

#include <lmcore/task_queue.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;
class EventHandler;

class UdpServerImpl final : public BaseServer, public std::enable_shared_from_this<UdpServerImpl> {
    friend class UdpServerHandler;

public:
    UdpServerImpl(std::string ip, uint16_t port);
    ~UdpServerImpl();

    // impl IUdpServer
    bool Init() override;
    bool Start() override;
    bool Stop() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }

    bool Send(std::string ip, uint16_t port, const void *data, size_t len);
    bool Send(std::string ip, uint16_t port, const std::string &str);
    bool Send(std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data);
    socket_t GetSocketFd() const override
    {
        // Return a valid socket fd if available (prefer IPv4)
        if (ipv4_socket_ != INVALID_SOCKET) {
            return ipv4_socket_;
        }
        return ipv6_socket_;
    }

private:
    void HandleReceive(socket_t fd);

private:
    std::string ip_;
    uint16_t port_;

    // Dual-stack sockets
    socket_t ipv4_socket_ = INVALID_SOCKET;
    socket_t ipv6_socket_ = INVALID_SOCKET;

    struct sockaddr_in server_addr4_;
    struct sockaddr_in6 server_addr6_;

    std::weak_ptr<IServerListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    // Separate handlers for IPv4 and IPv6
    std::shared_ptr<EventHandler> ipv4_handler_;
    std::shared_ptr<EventHandler> ipv6_handler_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
