/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_UNIX_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_UNIX_CLIENT_IMPL_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "iunix_client.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {

class UnixClientImpl final : public IUnixClient, public std::enable_shared_from_this<UnixClientImpl> {
public:
    explicit UnixClientImpl(const std::string &socketPath);
    ~UnixClientImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool Connect() override;

    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    bool SendFds(const std::vector<int> &fds) override;
    bool SendWithFds(std::shared_ptr<DataBuffer> data, const std::vector<int> &fds) override;

    void Close() override;

    socket_t GetSocketFd() const override { return socket_; }

private:
    void HandleConnect(int result);
    void StartReceive();
    void HandleReceiveWithFds(std::shared_ptr<DataBuffer> buffer, int bytes_read, std::vector<int> fds);
    void HandleClose();

    // Common Unix Socket send method
    bool SendUnixMessage(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds);

private:
    std::string socketPath_;
    socket_t socket_ = INVALID_SOCKET;

    std::atomic_bool isRunning_{false};
    std::atomic_bool isConnected_{false};

    std::weak_ptr<IClientListener> listener_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_UNIX_CLIENT_IMPL_H
