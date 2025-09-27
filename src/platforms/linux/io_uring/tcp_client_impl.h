/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H

#include <lmcore/data_buffer.h>
#include <lmcore/task_queue.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "itcp_client.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"
#include "platforms/linux/io_uring/tcp_client_impl.h"

namespace lmshao::lmnet {
using namespace lmshao::lmcore;

class TcpClientImpl final : public ITcpClient,
                            public std::enable_shared_from_this<TcpClientImpl>,
                            public Creatable<TcpClientImpl> {
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

private:
    void StartAsyncRead();

protected:
    TcpClientImpl(std::string remote_ip, uint16_t remote_port, std::string local_ip = "", uint16_t local_port = 0);

private:
    void SubmitConnect();
    void HandleConnect(int result);
    void SubmitRead();
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read);
    void HandleClose(bool is_error, const std::string &reason);
    void ReInit();

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    int socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;
    struct sockaddr_in localAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> task_queue_;
    std::atomic_bool is_running_{false};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_TCP_CLIENT_IMPL_H