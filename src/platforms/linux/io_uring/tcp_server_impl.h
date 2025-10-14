/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H

#include <lmcore/task_queue.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskQueue;

class TcpServerImpl : public BaseServer, public std::enable_shared_from_this<TcpServerImpl> {
public:
    TcpServerImpl(std::string local_ip, uint16_t local_port);
    ~TcpServerImpl() override;

    bool Init() override;
    bool Start() override;
    bool Stop() override;

    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }

    void Disconnect(socket_t fd);

    socket_t GetSocketFd() const override { return socket_; }

private:
    void SubmitAccept();
    void HandleAccept(int res, const sockaddr_in &client_addr);
    void SubmitRead(std::shared_ptr<Session> session);
    void HandleReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer, int bytes_read);
    void HandleConnectionClose(int client_fd, const std::string &reason);

private:
    socket_t socket_ = INVALID_SOCKET;
    std::string localIp_;
    uint16_t localPort_;
    std::weak_ptr<IServerListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::atomic_bool isRunning_{false};

    std::unordered_map<socket_t, std::shared_ptr<Session>> sessions_;
    std::mutex sessionMutex_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_TCP_SERVER_IMPL_H