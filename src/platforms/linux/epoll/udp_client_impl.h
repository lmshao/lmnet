/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H

#include <lmcore/task_queue.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <memory>
#include <string>

#include "iudp_client.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;
class EventHandler;

class UdpClientImpl : public IUdpClient, public std::enable_shared_from_this<UdpClientImpl> {
    friend class UdpClientHandler;

public:
    UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);
    ~UdpClientImpl();

    // impl IUdpClient
    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool EnableBroadcast() override;

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override;
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
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    // Dual-stack sockets
    socket_t ipv4_socket_ = INVALID_SOCKET;
    socket_t ipv6_socket_ = INVALID_SOCKET;

    // Cached remote address
    struct sockaddr_storage remote_addr_ {};
    socklen_t remote_addr_len_ = 0;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    // Separate handlers for IPv4 and IPv6
    std::shared_ptr<EventHandler> ipv4_handler_;
    std::shared_ptr<EventHandler> ipv6_handler_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H