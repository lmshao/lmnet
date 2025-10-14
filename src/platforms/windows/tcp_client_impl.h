/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_TCP_CLIENT_IMPL_H
#define LMSHAO_LMNET_TCP_CLIENT_IMPL_H

#include <lmcore/task_queue.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "itcp_client.h"
#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;

class TcpClientImpl final : public ITcpClient, public std::enable_shared_from_this<TcpClientImpl> {

public:
    TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);
    ~TcpClientImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override;
    bool Connect() override;
    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override;
    socket_t GetSocketFd() const override;

private:
    void ReInit();
    void SubmitConnect();
    void SubmitRead();
    void HandleConnect(DWORD error);
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError);
    void HandleSend(DWORD bytesOrError);
    void HandleClose(bool isError, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;
    std::string localIp_;
    uint16_t localPort_;

    SOCKET socket_{INVALID_SOCKET};
    sockaddr_in serverAddr_{};
    sockaddr_in localAddr_{};

    std::atomic<bool> isRunning_{false};
    std::atomic<bool> isConnected_{false};
    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;

    // Connect synchronization to provide blocking semantics similar to other platforms
    std::mutex connectMutex_;
    std::condition_variable connectCond_;
    bool connectPending_{false};
    bool connectSuccess_{false};
    DWORD lastConnectError_{0};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_TCP_CLIENT_IMPL_H
