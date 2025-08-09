/**
 * @file tcp_server_impl.h
 * @brief TCP Server Linux Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LINUX_TCP_SERVER_IMPL_H
#define NETWORK_LINUX_TCP_SERVER_IMPL_H

#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "common.h"
#include "iserver_listener.h"
#include "itcp_server.h"
#include "session.h"
#include "task_queue.h"

namespace lmshao::network {
class EventHandler;
class TcpConnectionHandler;

class TcpServerImpl final : public ITcpServer,
                            public BaseServer,
                            public std::enable_shared_from_this<TcpServerImpl>,
                            public Creatable<TcpServerImpl> {
    friend class EventProcessor;
    friend class TcpServerHandler;
    friend class TcpConnectionHandler;
    friend class Creatable<TcpServerImpl>;

public:
    ~TcpServerImpl();

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Start() override;
    bool Stop() override;
    bool Send(int fd, std::string host, uint16_t port, const void *data, size_t size) override;
    bool Send(int fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) override;
    bool Send(int fd, std::string host, uint16_t port, const std::string &str) override;

    socket_t GetSocketFd() const override { return socket_; }

protected:
    TcpServerImpl(std::string listenIp, uint16_t listenPort) : localPort_(listenPort), localIp_(listenIp) {}
    explicit TcpServerImpl(uint16_t listenPort) : localPort_(listenPort) {}

    void HandleAccept(int fd);
    void HandleReceive(int fd);
    void HandleConnectionClose(int fd, bool isError, const std::string &reason);
    void EnableKeepAlive(int fd);

private:
    uint16_t localPort_;
    int socket_ = INVALID_SOCKET;
    std::string localIp_ = "0.0.0.0";
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IServerListener> listener_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::unique_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<EventHandler> serverHandler_;
    std::unordered_map<int, std::shared_ptr<TcpConnectionHandler>> connectionHandlers_;
};

} // namespace lmshao::network

#endif // NETWORK_LINUX_TCP_SERVER_IMPL_H