/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_LINUX_UDP_SERVER_IMPL_H
#define LMSHAO_NETWORK_LINUX_UDP_SERVER_IMPL_H

#include <coreutils/data_buffer.h>
#include <coreutils/task_queue.h>
#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "../../base_server.h"
#include "network/common.h"
#include "network/iserver_listener.h"
#include "network/session.h"

namespace lmshao::network {
using namespace lmshao::coreutils;
class EventHandler;

class UdpServerImpl final : public BaseServer,
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

    bool Send(socket_t fd, std::string ip, uint16_t port, const void *data, size_t len) override;
    bool Send(socket_t fd, std::string ip, uint16_t port, const std::string &str) override;
    bool Send(socket_t fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) override;
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

#endif // LMSHAO_NETWORK_LINUX_UDP_SERVER_IMPL_H
