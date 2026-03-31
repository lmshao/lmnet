/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_EPOLL_TCP_SESSION_IMPL_H
#define LMSHAO_LMNET_LINUX_EPOLL_TCP_SESSION_IMPL_H

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "lmnet/common.h"
#include "lmnet/session.h"
#include "tcp_server_impl.h"

namespace lmshao::lmnet {

class TcpSessionImpl : public Session {
public:
    TcpSessionImpl(socket_t fd, std::string host, uint16_t port, std::shared_ptr<TcpServerImpl> server)
        : server_(server)
    {
        SetNativeHandle(fd);
        SetPeer(std::move(host), port);
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        auto server = server_.lock();
        if (!server || !buffer) {
            return false;
        }
        return server->Send(NativeHandle(), std::move(buffer));
    }

    bool Send(const std::string &str) const override
    {
        auto server = server_.lock();
        return server ? server->Send(NativeHandle(), str) : false;
    }

    bool Send(const void *data, size_t size) const override
    {
        auto server = server_.lock();
        return server ? server->Send(NativeHandle(), data, size) : false;
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << Host() << ":" << Port() << " (" << NativeHandle() << ")";
        return ss.str();
    }

private:
    std::weak_ptr<TcpServerImpl> server_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_EPOLL_TCP_SESSION_IMPL_H
