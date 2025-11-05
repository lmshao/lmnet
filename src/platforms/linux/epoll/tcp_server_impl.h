/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H

#include <lmcore/task_queue.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;
class EventHandler;
class TcpConnectionHandler;
class TcpServerImpl final : public BaseServer, public std::enable_shared_from_this<TcpServerImpl> {
    friend class EventProcessor;
    friend class TcpServerHandler;
    friend class TcpConnectionHandler;

public:
    TcpServerImpl(std::string listenIp, uint16_t listenPort) : localPort_(listenPort), localIp_(listenIp) {}
    explicit TcpServerImpl(uint16_t listenPort) : localPort_(listenPort) {}
    ~TcpServerImpl();

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Start() override;
    bool Stop() override;
    bool Send(socket_t fd, const void *data, size_t size);
    bool Send(socket_t fd, std::shared_ptr<DataBuffer> buffer);
    bool Send(socket_t fd, const std::string &str);

    socket_t GetSocketFd() const override
    {
        // Return a valid listening socket fd if available (prefer IPv4)
        if (ipv4_socket_ != INVALID_SOCKET) {
            return ipv4_socket_;
        }
        return ipv6_socket_;
    }

private:
    void HandleAccept(socket_t fd);
    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);
    void EnableKeepAlive(socket_t fd);

private:
    uint16_t localPort_;
    std::string localIp_ = "0.0.0.0";
    // Dual-stack listening sockets
    socket_t ipv4_socket_ = INVALID_SOCKET;
    socket_t ipv6_socket_ = INVALID_SOCKET;
    struct sockaddr_in server_addr4_ {};
    struct sockaddr_in6 server_addr6_ {};

    std::weak_ptr<IServerListener> listener_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::unique_ptr<DataBuffer> readBuffer_;

    // Separate handlers for IPv4 and IPv6 listening sockets
    std::shared_ptr<EventHandler> ipv4_handler_;
    std::shared_ptr<EventHandler> ipv6_handler_;
    std::unordered_map<int, std::shared_ptr<TcpConnectionHandler>> connectionHandlers_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H