/**
 * @file unix_client_impl.h
 * @brief Unix Client Linux Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LINUX_UNIX_CLIENT_IMPL_H
#define NETWORK_LINUX_UNIX_CLIENT_IMPL_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <sys/un.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "data_buffer.h"
#include "iclient_listener.h"
#include "iunix_client.h"
#include "task_queue.h"

namespace lmshao::network {

class UnixClientHandler;
class EventHandler;

class UnixClientImpl final : public IUnixClient,
                             public std::enable_shared_from_this<UnixClientImpl>,
                             public Creatable<UnixClientImpl> {
    friend class UnixClientHandler;
    friend class Creatable<UnixClientImpl>;

public:
    ~UnixClientImpl();

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool Connect() override;

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;

    void Close() override;

    socket_t GetSocketFd() const override { return socket_; }

protected:
    UnixClientImpl(const std::string &socketPath);

    void HandleReceive(int fd);
    void HandleConnectionClose(int fd, bool isError, const std::string &reason);

private:
    std::string socketPath_;
    socket_t socket_ = INVALID_SOCKET;
    struct sockaddr_un serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<UnixClientHandler> clientHandler_;
};

} // namespace lmshao::network

#endif // NETWORK_LINUX_UNIX_CLIENT_IMPL_H
