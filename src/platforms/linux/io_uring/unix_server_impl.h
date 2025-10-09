/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UNIX_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_UNIX_SERVER_IMPL_H

#include <lmcore/data_buffer.h>
#include <sys/un.h>

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "base_server.h"
#include "lmnet/common.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {

class UnixServerImpl final : public BaseServer,
                             public std::enable_shared_from_this<UnixServerImpl>,
                             public Creatable<UnixServerImpl> {
public:
    explicit UnixServerImpl(const std::string &socketPath);
    ~UnixServerImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Start() override;
    bool Stop() override;

    socket_t GetSocketFd() const override { return socket_; }

private:
    void SubmitAccept();
    void HandleAccept(int client_fd);
    void SubmitRead(int client_fd);
    void HandleReceive(int client_fd, std::shared_ptr<DataBuffer> buffer, int bytes_read);
    void HandleConnectionClose(int client_fd);

private:
    std::string socketPath_;
    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_un serverAddr_;

    std::atomic_bool isRunning_{false};

    std::weak_ptr<IServerListener> listener_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UNIX_SERVER_IMPL_H
