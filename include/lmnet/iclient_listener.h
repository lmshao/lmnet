/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_ICLIENT_LISTENER_H
#define LMSHAO_LMNET_ICLIENT_LISTENER_H

#include <memory>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include "common.h"

namespace lmshao::lmnet {

class IClientListener {
public:
    virtual ~IClientListener() = default;

    /**
     * @brief Called when data is received
     * @param fd Socket file descriptor
     * @param buffer Received data buffer
     */
    virtual void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> buffer) = 0;

#if defined(__linux__) || defined(__APPLE__)
    /**
     * @brief Called when file descriptors are received
     * @param fd Socket file descriptor
     * @param fds File descriptors passed from the peer
     */
    virtual void OnReceiveFds(socket_t fd, std::vector<int> fds)
    {
        for (int descriptor : fds) {
            if (descriptor >= 0) {
                ::close(descriptor);
            }
        }
    }
#endif

    /**
     * @brief Called when connection is closed
     * @param fd Socket file descriptor
     */
    virtual void OnClose(socket_t fd) = 0;

    /**
     * @brief Called when an error occurs
     * @param fd Socket file descriptor
     * @param errorInfo Error information
     */
    virtual void OnError(socket_t fd, const std::string &errorInfo) = 0;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_ICLIENT_LISTENER_H