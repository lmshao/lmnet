/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_LINUX_TCP_CLIENT_IMPL_H
#define LMSHAO_NETWORK_LINUX_TCP_CLIENT_IMPL_H

#include <coreutils/data_buffer.h>
#include <coreutils/task_queue.h>
#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "../../itcp_client.h"
#include "network/common.h"
#include "network/iclient_listener.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

class TcpClientHandler;
class EventHandler;
class TcpClientImpl final : public ITcpClient,
                            public std::enable_shared_from_this<TcpClientImpl>,
                            public Creatable<TcpClientImpl> {
    friend class TcpClientHandler;
    friend class Creatable<TcpClientImpl>;

public:
    ~TcpClientImpl();

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool Connect() override;

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;

    void Close() override;

    int GetSocketFd() const override { return socket_; }

protected:
    TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);
    void ReInit();

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    int socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<TcpClientHandler> clientHandler_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_LINUX_TCP_CLIENT_IMPL_H