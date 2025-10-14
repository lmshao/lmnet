/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_TCP_SERVER_IMPL_H
#define LMSHAO_LMNET_TCP_SERVER_IMPL_H

#include <lmcore/task_queue.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "base_server.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;

class TcpServerImpl final : public BaseServer, public std::enable_shared_from_this<TcpServerImpl> {

public:
    TcpServerImpl(std::string listenIp, uint16_t listenPort);
    TcpServerImpl(uint16_t listenPort);
    ~TcpServerImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override;
    bool Start() override;
    bool Stop() override;
    socket_t GetSocketFd() const override;

    bool Send(socket_t fd, const void *data, size_t size);
    bool Send(socket_t fd, std::shared_ptr<DataBuffer> buffer);
    bool Send(socket_t fd, const std::string &str);

private:
    void SubmitAccept();
    void SubmitRead(SOCKET clientSocket);
    void HandleAccept(SOCKET clientSocket, const sockaddr_in &clientAddr);
    void HandleReceive(SOCKET clientSocket, std::shared_ptr<DataBuffer> buffer, DWORD bytesOrError);
    void HandleSend(SOCKET clientSocket, DWORD bytesOrError);
    void HandleClientClose(SOCKET clientSocket, bool isError, const std::string &reason);

    // Session management
    void AddSession(SOCKET socket, std::shared_ptr<Session> session);
    void RemoveSession(SOCKET socket);
    std::shared_ptr<Session> GetSession(SOCKET socket);

private:
    std::string ip_;
    uint16_t port_;

    SOCKET listenSocket_{INVALID_SOCKET};
    sockaddr_in listenAddr_{};

    std::atomic<bool> isRunning_{false};
    std::weak_ptr<IServerListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;

    // Session management
    std::mutex sessionMutex_;
    std::unordered_map<SOCKET, std::shared_ptr<Session>> sessions_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_TCP_SERVER_IMPL_H
