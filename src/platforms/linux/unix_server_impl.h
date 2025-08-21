/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_LINUX_UNIX_SERVER_IMPL_H
#define LMSHAO_NETWORK_LINUX_UNIX_SERVER_IMPL_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <coreutils/task_queue.h>
#include <sys/un.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "base_server.h"
#include "network/common.h"
#include "network/iserver_listener.h"
#include "network/session.h"

namespace lmshao::network {
using namespace lmshao::coreutils;
class EventHandler;
class UnixConnectionHandler;

class UnixServerImpl final : public BaseServer,
                             public std::enable_shared_from_this<UnixServerImpl>,
                             public Creatable<UnixServerImpl> {
    friend class EventProcessor;
    friend class UnixServerHandler;
    friend class UnixConnectionHandler;
    friend class Creatable<UnixServerImpl>;

public:
    ~UnixServerImpl();

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Start() override;
    bool Stop() override;
    bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size) override;
    bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) override;
    bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str) override;

    socket_t GetSocketFd() const override { return socket_; }

protected:
    UnixServerImpl(const std::string &socketPath);

    void HandleAccept(socket_t fd);
    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool isError, const std::string &reason);

private:
    std::string socketPath_;
    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_un serverAddr_;

    std::weak_ptr<IServerListener> listener_;
    std::unordered_map<int, std::shared_ptr<Session>> sessions_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<EventHandler> serverHandler_;
    std::unordered_map<int, std::shared_ptr<UnixConnectionHandler>> connectionHandlers_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_LINUX_UNIX_SERVER_IMPL_H
