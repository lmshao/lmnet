/**
 * @file udp_server.h
 * @brief UDP Server Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_UDP_SERVER_H
#define NETWORK_UDP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "core-utils/data_buffer.h"
#include "iserver_listener.h"

namespace lmshao::network {

class BaseServer;
class UdpServer final : public Creatable<UdpServer> {
public:
    UdpServer(std::string listenIp, uint16_t listenPort);
    explicit UdpServer(uint16_t listenPort);
    ~UdpServer();

    bool Init();
    bool Start();
    bool Stop();
    void SetListener(std::shared_ptr<IServerListener> listener);

    socket_t GetSocketFd() const;

    static uint16_t GetIdlePort();
    static uint16_t GetIdlePortPair();

private:
    std::shared_ptr<BaseServer> impl_;
};

} // namespace lmshao::network

#endif // NETWORK_UDP_SERVER_H
