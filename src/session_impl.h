/**
 * @file session_impl.h
 * @brief Session Implementation Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_SESSION_IMPL_H
#define NETWORK_SESSION_IMPL_H

#include "session.h"

namespace lmshao::network {
class SessionImpl : public Session {
public:
    SessionImpl(socket_t fd, std::string host, uint16_t port, std::shared_ptr<BaseServer> server) : server_(server)
    {
        this->host = host;
        this->port = port;
        this->fd = fd;
    }

    virtual ~SessionImpl() = default;

    bool Send(const void *data, size_t size) const override
    {
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, data, size);
        }
        return false;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, buffer);
        }
        return false;
    }

    bool Send(const std::string &str) const override
    {
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, str);
        }
        return false;
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port << " (" << fd << ")";
        return ss.str();
    }

private:
    std::weak_ptr<BaseServer> server_;
};

} // namespace lmshao::network
#endif // NETWORK_SESSION_IMPL_H