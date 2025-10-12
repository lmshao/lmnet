/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_ISERVER_LISTENER_H
#define LMSHAO_LMNET_ISERVER_LISTENER_H

#include <memory>

#include "session.h"

namespace lmshao::lmnet {

class IServerListener {
public:
    virtual ~IServerListener() = default;

    virtual void OnReceiveFrom(const std::string &ip, uint16_t port, std::shared_ptr<DataBuffer> buffer) {};

    /**
     * @brief Called when an error occurs
     * @param session Session that encountered the error
     * @param errorInfo Error information
     */
    virtual void OnError(std::shared_ptr<Session> session, const std::string &errorInfo) = 0;

    /**
     * @brief Called when a session is closed
     * @param session Closed session
     */
    virtual void OnClose(std::shared_ptr<Session> session) = 0;

    /**
     * @brief Called when a new connection is accepted
     * @param session New session
     */
    virtual void OnAccept(std::shared_ptr<Session> session) = 0;

    /**
     * @brief Called when data is received
     * @param session Session that received data
     * @param buffer Received data buffer
     */
    virtual void OnReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer) = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_ISERVER_LISTENER_H