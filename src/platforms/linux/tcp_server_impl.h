/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_LINUX_TCP_SERVER_IMPL_H
#define LMSHAO_NETWORK_LINUX_TCP_SERVER_IMPL_H

#include <coreutils/task_queue.h>
#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "network/common.h"
#include "network/iserver_listener.h"
#include "network/session.h"

namespace lmshao::network {
using namespace lmshao::coreutils;
class EventHandler;
class TcpConnectionHandler;
class TcpServerImpl final : public BaseServer,
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
    bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size) override;
    bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) override;
    bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str) override;

    socket_t GetSocketFd() const override { return socket_; }

protected:
    TcpServerImpl(std::string listenIp, uint16_t listenPort) : localPort_(listenPort), localIp_(listenIp) {}
    explicit TcpServerImpl(uint16_t listenPort) : localPort_(listenPort) {}

    void HandleAccept(socket_t fd);
    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);
    void EnableKeepAlive(socket_t fd);

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

#endif // LMSHAO_NETWORK_LINUX_TCP_SERVER_IMPL_H