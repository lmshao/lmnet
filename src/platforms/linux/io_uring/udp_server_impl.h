/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H

#include <lmcore/data_buffer.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"

namespace lmshao::lmnet {

class UdpServerImpl final : public BaseServer,
                            public std::enable_shared_from_this<UdpServerImpl>,
                            public Creatable<UdpServerImpl> {
    friend class Creatable<UdpServerImpl>;

public:
    ~UdpServerImpl();

    // impl IUdpServer
    bool Init() override;
    bool Start() override;
    bool Stop() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }

    socket_t GetSocketFd() const override { return socket_; }

protected:
    UdpServerImpl(std::string ip, uint16_t port);

private:
    void StartReceive();
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr);

private:
    std::string ip_;
    uint16_t port_;

    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IServerListener> listener_;
    std::atomic_bool isRunning_{false};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UDP_SERVER_IMPL_H
