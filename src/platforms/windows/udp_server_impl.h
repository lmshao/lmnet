/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UDP_SERVER_IMPL_H
#define LMSHAO_LMNET_UDP_SERVER_IMPL_H

#include <lmcore/task_queue.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "base_server.h"
#include "lmnet/iserver_listener.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;

class UdpServerImpl final : public BaseServer, public std::enable_shared_from_this<UdpServerImpl> {

public:
    UdpServerImpl(std::string listenIp, uint16_t listenPort);
    UdpServerImpl(uint16_t listenPort);
    ~UdpServerImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override;
    bool Start() override;
    bool Stop() override;
    socket_t GetSocketFd() const override;

    bool Send(std::string host, uint16_t port, const void *data, size_t size);
    bool Send(std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer);
    bool Send(std::string host, uint16_t port, const std::string &str);

private:
    void StartReceiving();
    void SubmitReceive();
    void HandleReceive(std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError, const sockaddr_in &fromAddr);
    void HandleSend(DWORD bytesOrError);

private:
    std::string ip_;
    uint16_t port_;

    SOCKET socket_{INVALID_SOCKET};
    sockaddr_in listenAddr_{};

    std::atomic<bool> isRunning_{false};
    std::weak_ptr<IServerListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;

    // Number of concurrent receive operations
    static constexpr int CONCURRENT_RECEIVES = 8;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_UDP_SERVER_IMPL_H
