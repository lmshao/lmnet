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

namespace lmshao::lmnet {

class IoUringSessionImpl : public Session {
public:
    IoUringSessionImpl(socket_t fd, std::string host, uint16_t port)
    {
        this->host = std::move(host);
        this->port = port;
        this->fd = fd;
    }

    IoUringSessionImpl(socket_t server_socket, std::string remote_host, uint16_t remote_port, bool is_udp)
        : is_udp_(is_udp)
    {
        this->host = std::move(remote_host);
        this->port = remote_port;
        this->fd = server_socket;

        if (is_udp_) {
            memset(&client_addr_, 0, sizeof(client_addr_));
            client_addr_.sin_family = AF_INET;
            client_addr_.sin_port = htons(remote_port);
            client_addr_.sin_addr.s_addr = inet_addr(host.c_str());
        }
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
            return IoUringManager::GetInstance().SubmitSendToRequest(fd, buffer, client_addr_, [](int, int res) {
                if (res < 0) {
                    LMNET_LOGE("UDP Send failed: %s", strerror(-res));
                }
            });
        }
        return IoUringManager::GetInstance().SubmitWriteRequest(fd, buffer, nullptr);
    }

    bool Send(const std::string &str) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(str.data(), str.size());
        return Send(buffer);
    }

    bool SendFds(const std::vector<int> &fds) const override
    {
        (void)fds;
        LMNET_LOGE("SendFds is not supported in io_uring backend");
        return false;
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port << " (" << fd << ")";
        return ss.str();
    }

private:
    bool is_udp_ = false;
    sockaddr_in client_addr_{};
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H
