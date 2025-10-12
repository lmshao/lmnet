/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_I_UDP_CLIENT_H
#define LMSHAO_LMNET_I_UDP_CLIENT_H

#include <cstdint>
#include <memory>
#include <string>

#include "lmnet/iclient_listener.h"

namespace lmshao::lmnet {

class IUdpClient {
public:
    virtual ~IUdpClient() = default;

    virtual bool Init() = 0;
    virtual void SetListener(std::shared_ptr<IClientListener> listener) = 0;
    virtual bool EnableBroadcast() = 0;
    virtual bool Send(const std::string &str) = 0;
    virtual bool Send(const void *data, size_t len) = 0;
    virtual bool Send(std::shared_ptr<DataBuffer> data) = 0;
    virtual void Close() = 0;
    virtual socket_t GetSocketFd() const = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_I_UDP_CLIENT_H