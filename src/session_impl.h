/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_SESSION_IMPL_H
#define LMSHAO_NETWORK_SESSION_IMPL_H

#include "base_server.h"
#include "network/session.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

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
#endif // LMSHAO_NETWORK_SESSION_IMPL_H