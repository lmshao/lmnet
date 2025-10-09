/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_SESSION_IMPL_H
#define LMSHAO_LMNET_SESSION_IMPL_H

#ifdef LMNET_LINUX_BACKEND_IOURING
#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>

#include "internal_logger.h"
#include "platforms/linux/io_uring/io_uring_manager.h"
#else
#include "base_server.h"
#endif
#include "lmnet/session.h"

namespace lmshao::lmnet {
using namespace lmshao::lmcore;

class SessionImpl : public Session {
public:
#ifdef LMNET_LINUX_BACKEND_IOURING
    // io_uring constructor - no server dependency
    SessionImpl(socket_t fd, std::string host, uint16_t port)
    {
        this->host = std::move(host);
        this->port = port;
        this->fd = fd;
    }

    // UDP-specific constructor with client address
    SessionImpl(socket_t server_socket, std::string remote_host, uint16_t remote_port, bool is_udp) : is_udp_(is_udp)
    {
        this->host = std::move(remote_host);
        this->port = remote_port;
        this->fd = server_socket;

        if (is_udp_) {
            // Pre-construct client address for UDP
            memset(&client_addr_, 0, sizeof(client_addr_));
            client_addr_.sin_family = AF_INET;
            client_addr_.sin_port = htons(remote_port);
            client_addr_.sin_addr.s_addr = inet_addr(host.c_str());
        }
    }
#else
    // Traditional platforms constructor - requires server
    SessionImpl(socket_t fd, std::string host, uint16_t port, std::shared_ptr<BaseServer> server) : server_(server)
    {
        this->host = std::move(host);
        this->port = port;
        this->fd = fd;
    }
#endif

    virtual ~SessionImpl() = default;

    bool Send(const void *data, size_t size) const override
    {
#ifdef LMNET_LINUX_BACKEND_IOURING
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(data, size);
        return Send(buffer);
#else
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, data, size);
        }
        return false;
#endif
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
#ifdef LMNET_LINUX_BACKEND_IOURING
        if (is_udp_) {
            // UDP: use SubmitSendToRequest with client address
            return IoUringManager::GetInstance().SubmitSendToRequest(fd, buffer, client_addr_, [](int, int res) {
                if (res < 0) {
                    LMNET_LOGE("UDP Send failed: %s", strerror(-res));
                }
            });
        } else {
            // TCP/Unix Socket: use SubmitWriteRequest
            return IoUringManager::GetInstance().SubmitWriteRequest(fd, buffer, nullptr);
        }
#else
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, buffer);
        }
        return false;
#endif
    }

    bool Send(const std::string &str) const override
    {
#ifdef LMNET_LINUX_BACKEND_IOURING
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(str.data(), str.size());
        return Send(buffer);
#else
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, str);
        }
        return false;
#endif
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port << " (" << fd << ")";
        return ss.str();
    }

private:
#ifdef LMNET_LINUX_BACKEND_IOURING
    bool is_udp_ = false;       // Flag to distinguish UDP from TCP/Unix Socket
    sockaddr_in client_addr_{}; // Pre-constructed client address for UDP
#else
    std::weak_ptr<BaseServer> server_;
#endif
};

} // namespace lmshao::lmnet
#endif // LMSHAO_LMNET_SESSION_IMPL_H