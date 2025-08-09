/**
 * @file udp_client_impl.h
 * @brief UDP Client Linux Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LINUX_UDP_CLIENT_IMPL_H
#define NETWORK_LINUX_UDP_CLIENT_IMPL_H

#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iclient_listener.h"
#include "iudp_client.h"
#include "task_queue.h"

namespace lmshao::network {

class EventHandler;

class UdpClientImpl final : public IUdpClient,
                            public std::enable_shared_from_this<UdpClientImpl>,
                            public Creatable<UdpClientImpl> {
    friend class UdpClientHandler;
    friend class Creatable<UdpClientImpl>;

public:
    ~UdpClientImpl();

    // impl IUdpClient
    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override;
    socket_t GetSocketFd() const override { return socket_; }

protected:
    // Constructor should be protected in IMPL pattern
    UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

private:
    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<EventHandler> clientHandler_;
};

} // namespace lmshao::network

#endif // NETWORK_LINUX_UDP_CLIENT_IMPL_H