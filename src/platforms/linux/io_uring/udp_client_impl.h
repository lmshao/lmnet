/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H

#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "iudp_client.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {

class UdpClientImpl : public IUdpClient, public std::enable_shared_from_this<UdpClientImpl> {
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
    socket_t GetSocketFd() const override { return socket_; }

private:
    void StartReceive();
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr);
    void HandleClose(bool is_error, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;
    struct sockaddr_in localAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::atomic_bool isRunning_{false};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UDP_CLIENT_IMPL_H