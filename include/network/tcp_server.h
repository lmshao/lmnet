/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_TCP_SERVER_H
#define LMSHAO_NETWORK_TCP_SERVER_H

#include <coreutils/data_buffer.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class BaseServer;
class TcpServer final : public Creatable<TcpServer> {
public:
    TcpServer(std::string listenIp, uint16_t listenPort);
    explicit TcpServer(uint16_t listenPort);

    ~TcpServer();

    bool Init();
    void SetListener(std::shared_ptr<IServerListener> listener);
    bool Start();
    bool Stop();
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<BaseServer> impl_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_TCP_SERVER_H