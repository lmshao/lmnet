/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_EPOLL_UNIX_SESSION_IMPL_H
#define LMSHAO_LMNET_LINUX_EPOLL_UNIX_SESSION_IMPL_H

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "lmnet/common.h"
#include "lmnet/session.h"
#include "unix_server_impl.h"

namespace lmshao::lmnet {

class UnixSessionImpl : public Session {
public:
    UnixSessionImpl(socket_t fd, std::string path, std::shared_ptr<UnixServerImpl> server) : server_(server)
    {
        this->fd = fd;
        this->host = std::move(path);
        this->port = 0;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        auto server = server_.lock();
        if (!server || !buffer) {
            return false;
        }
        return server->Send(fd, std::move(buffer));
    }

    bool Send(const std::string &str) const override
    {
        auto server = server_.lock();
        return server ? server->Send(fd, str) : false;
    }

    bool Send(const void *data, size_t size) const override
    {
        auto server = server_.lock();
        return server ? server->Send(fd, data, size) : false;
    }

    bool SendFds(const std::vector<int> &fds) const override
    {
        auto server = server_.lock();
        return server ? server->SendFds(fd, fds) : false;
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << " (" << fd << ")";
        return ss.str();
    }

private:
    std::weak_ptr<UnixServerImpl> server_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_EPOLL_UNIX_SESSION_IMPL_H
