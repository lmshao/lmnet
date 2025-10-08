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
#include "lmnet/session.h"

namespace lmshao::lmnet {
using namespace lmshao::lmcore;

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

    bool Send(const std::string &ip, uint16_t port, const void *data, size_t len);
    bool Send(const std::string &ip, uint16_t port, const std::string &str);
    bool Send(const std::string &ip, uint16_t port, std::shared_ptr<DataBuffer> data);
    socket_t GetSocketFd() const override { return socket_; }

    // To satisfy BaseServer interface
    bool Send(socket_t fd, std::string ip, uint16_t port, const void *data, size_t len) override;
    bool Send(socket_t fd, std::string ip, uint16_t port, const std::string &str) override;
    bool Send(socket_t fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data) override;

protected:
    // Constructor should be protected in IMPL pattern
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
