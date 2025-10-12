/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_BASE_SERVER_H
#define LMSHAO_LMNET_BASE_SERVER_H

#include <memory>
#include <vector>

#include "lmnet/common.h"

namespace lmshao::lmnet {

class IServerListener;
class BaseServer {
public:
    virtual ~BaseServer() = default;
    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IServerListener> listener) = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;

    virtual bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size)
    {
        (void)fd;
        (void)host;
        (void)port;
        (void)data;
        (void)size;
        return false;
    }

    virtual bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
    {
        (void)fd;
        (void)host;
        (void)port;
        (void)buffer;
        return false;
    }

    virtual bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str)
    {
        (void)fd;
        (void)host;
        (void)port;
        (void)str;
        return false;
    }

    virtual bool SendFds(socket_t fd, std::string host, uint16_t port, const std::vector<int> &fds)
    {
        (void)fd;
        (void)host;
        (void)port;
        (void)fds;
        return false;
    }
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_BASE_SERVER_H