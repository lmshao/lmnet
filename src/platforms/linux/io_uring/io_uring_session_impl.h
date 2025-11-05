/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H
#define LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "lmnet/session.h"
#include "unix_socket_utils.h"

namespace lmshao::lmnet {

class IoUringSessionImpl : public Session {
public:
    IoUringSessionImpl(socket_t fd, std::string host, uint16_t port)
    {
        this->host = std::move(host);
        this->port = port;
        this->fd = fd;
    }

    IoUringSessionImpl(socket_t server_socket, std::string remote_host, uint16_t remote_port,
                       const sockaddr_storage &remote_addr, socklen_t remote_len, bool is_udp)
        : is_udp_(is_udp), remote_addr_len_(remote_len)
    {
        this->host = std::move(remote_host);
        this->port = remote_port;
        this->fd = server_socket;
        memset(&remote_addr_, 0, sizeof(remote_addr_));
        memcpy(&remote_addr_, &remote_addr, std::min(remote_len, (socklen_t)sizeof(remote_addr_)));
    }

    ~IoUringSessionImpl() override = default;

    bool Send(const void *data, size_t size) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(data, size);
        return Send(buffer);
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        if (is_udp_) {
            return IoUringManager::GetInstance().SubmitSendToRequest(
                fd, buffer, reinterpret_cast<const sockaddr *>(&remote_addr_), remote_addr_len_, [](int, int res) {
                    if (res < 0) {
                        LMNET_LOGE("UDP Send failed: %s", strerror(-res));
                    }
                });
        }
        // Use unified SendUnixMessage for consistency (Unix Socket)
        return SendUnixMessage(buffer, {});
    }

    bool Send(const std::string &str) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(str.data(), str.size());
        return Send(buffer);
    }

    bool SendFds(const std::vector<int> &fds) const override
    {
        if (is_udp_) {
            LMNET_LOGE("SendFds is not supported on UDP sockets");
            return false;
        }

        return SendUnixMessage(nullptr, fds);
    }

    bool SendWithFds(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds) const override
    {
        if (is_udp_) {
            LMNET_LOGE("SendWithFds is not supported on UDP sockets");
            return false;
        }

        if ((!buffer || buffer->Size() == 0) && fds.empty()) {
            LMNET_LOGW("No data and no file descriptors to send");
            return true;
        }

        return SendUnixMessage(buffer, fds);
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port << " (" << fd << ")";
        return ss.str();
    }

private:
    // Common Unix Socket send method
    bool SendUnixMessage(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds) const
    {
        // Only duplicate file descriptors if we actually have them
        std::vector<int> duplicatedFds;
        if (!fds.empty()) {
            duplicatedFds = UnixSocketUtils::DuplicateFds(fds);
            if (duplicatedFds.empty()) {
                return false; // Failed to duplicate fds
            }
        }

        // Create a copy for cleanup callback before moving duplicatedFds
        std::vector<int> fdsForCleanup = duplicatedFds;
        return IoUringManager::GetInstance().SubmitSendMsgRequest(
            fd, buffer, std::move(duplicatedFds),
            UnixSocketUtils::CreateCleanupCallback(std::move(fdsForCleanup), "Send Unix message"));
    }

private:
    bool is_udp_ = false;
    sockaddr_storage remote_addr_{};
    socklen_t remote_addr_len_ = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H
