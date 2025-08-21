/**
 * @file iserver_listener.h
 * @brief Server Listener Interface
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_ISERVER_LISTENER_H
#define NETWORK_ISERVER_LISTENER_H

#include <memory>

#include "coreutils/data_buffer.h"
#include "session.h"

namespace lmshao::network {

using namespace lmshao::coreutils;

class IServerListener {
public:
    virtual ~IServerListener() = default;
    virtual void OnError(std::shared_ptr<Session> session, const std::string &errorInfo) = 0;
    virtual void OnClose(std::shared_ptr<Session> session) = 0;
    virtual void OnAccept(std::shared_ptr<Session> session) = 0;
    virtual void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) = 0;
};

} // namespace lmshao::network

#endif // NETWORK_ISERVER_LISTENER_H