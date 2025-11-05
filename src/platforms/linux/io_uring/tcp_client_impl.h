/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H

#include <lmcore/task_queue.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "itcp_client.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskQueue;

class TcpClientImpl : public ITcpClient, public std::enable_shared_from_this<TcpClientImpl> {
public:
    TcpClientImpl(std::string remote_ip, uint16_t remote_port, std::string local_ip = "", uint16_t local_port = 0);
    ~TcpClientImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool Connect() override;
    void Close() override;

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;

    socket_t GetSocketFd() const override { return socket_; }

private:
    void ReInit();
    void SubmitConnect();
    void HandleConnect(int result);
    void SubmitRead();
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read);
    void HandleClose(bool is_error, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;
    std::string localIp_;
    uint16_t localPort_;

    socket_t socket_ = INVALID_SOCKET;
    sockaddr_storage serverAddr_{};
    socklen_t serverAddrLen_ = 0;
    sockaddr_storage localAddr_{};
    socklen_t localAddrLen_ = 0;

    std::atomic_bool isRunning_{false};
    std::atomic_bool isConnected_{false};
    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H