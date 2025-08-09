/**
 * @file udp_server_impl.h
 * @brief UDP Server Linux Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LINUX_UDP_SERVER_IMPL_H
#define NETWORK_LINUX_UDP_SERVER_IMPL_H

#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "common.h"
#include "data_buffer.h"
#include "iserver_listener.h"
#include "iudp_server.h"
#include "session.h"
#include "task_queue.h"

namespace lmshao::network {

class EventHandler;

class UdpServerImpl final : public IUdpServer,
                            public BaseServer,
                            public std::enable_shared_from_this<UdpServerImpl>,
                            public Creatable<UdpServerImpl> {
    friend class UdpServerHandler;
    friend class Creatable<UdpServerImpl>;

public:
    ~UdpServerImpl();

    // impl IUdpServer
    bool Init() override;
    bool Start() override;
    bool Stop() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }

    bool Send(int fd, std::string ip, uint16_t port, const void *data, size_t len) override;
    bool Send(int fd, std::string ip, uint16_t port, const std::string &str) override;
    bool Send(int fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) override;
    socket_t GetSocketFd() const override { return socket_; }

protected:
    // Constructor should be protected in IMPL pattern
    UdpServerImpl(std::string ip, uint16_t port);

private:
    void HandleReceive(socket_t fd);

private:
    std::string ip_;
    uint16_t port_;

    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IServerListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<EventHandler> serverHandler_;
};

} // namespace lmshao::network

#endif // NETWORK_LINUX_UDP_SERVER_IMPL_H
