/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_BASE_SERVER_H
#define LMSHAO_LMNET_BASE_SERVER_H

#include <lmcore/data_buffer.h>

#include <memory>

#include "lmnet/common.h"

namespace lmshao::lmnet {
using namespace lmshao::lmcore;

class IServerListener;
class BaseServer {
public:
    virtual ~BaseServer() = default;
    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IServerListener> listener) = 0;
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, const void *data, size_t size) = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) = 0;
    virtual bool Send(socket_t fd, std::string host, uint16_t port, const std::string &str) = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_BASE_SERVER_H