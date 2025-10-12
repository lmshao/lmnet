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
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "session.h"

namespace lmshao::lmnet {

class IServerListener {
public:
    virtual ~IServerListener() = default;

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

#if defined(__unix__) || defined(__APPLE__)
    /**
     * @brief Called when file descriptors are received
     * @param session Session that received the descriptors
     * @param fds File descriptors passed from the peer
     */
    virtual void OnReceiveFds(std::shared_ptr<Session> session, std::vector<int> fds)
    {
        for (int descriptor : fds) {
            if (descriptor >= 0) {
                ::close(descriptor);
            }
        }
    }
#endif

    /**
     * @brief Called when a session is closed
     * @param session Closed session
     */
    virtual void OnClose(std::shared_ptr<Session> session) = 0;

    /**
     * @brief Called when an error occurs
     * @param session Session that encountered the error
     * @param errorInfo Error information
     */
    virtual void OnError(std::shared_ptr<Session> session, const std::string &errorInfo) = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_ISERVER_LISTENER_H