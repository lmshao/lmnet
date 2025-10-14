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
#include "unix_message.h"

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
     * @brief Called when Unix message (with data and/or file descriptors) is received
     * @param fd Socket file descriptor
     * @param message Unix message containing data and/or file descriptors
     *
     * @par Usage Scenarios:
     *
     * @par Scenario 1: Unix Socket with File Descriptor Transfer
     * Override OnReceiveUnixMessage to handle messages that may contain file descriptors.
     * This provides atomic handling of data and FDs together.
     * @code
     * class FdListener : public IClientListener {
     *     void OnReceive(socket_t, std::shared_ptr<DataBuffer>) override {
     *         // Required but can be minimal for FD transfer scenarios
     *     }
     *
     *     void OnReceiveUnixMessage(socket_t fd, const UnixMessage& msg) override {
     *         // Handle messages that may contain data and/or file descriptors
     *         if (msg.HasData() && msg.HasFds()) {
     *             ProcessProtocol(msg.data);
     *             ProcessFileDescriptors(msg.fds);
     *         }
     *         // Remember to close FDs when done
     *         for (int fd : msg.fds) close(fd);
     *     }
     * };
     * @endcode
     *
     * @par Scenario 2: Unix Socket without File Descriptor Transfer
     * For standard Unix Socket communication (no FD transfer), just use OnReceive.
     * Leave OnReceiveUnixMessage with default implementation.
     * @code
     * class StandardListener : public IClientListener {
     *     void OnReceive(socket_t fd, std::shared_ptr<DataBuffer> buffer) override {
     *         ProcessData(buffer);  // Normal message handling
     *     }
     *     // OnReceiveUnixMessage: use default (does nothing)
     * };
     * @endcode
     *
     * @warning Do NOT override both OnReceive AND OnReceiveUnixMessage to process
     *          the same messages, as this will cause duplicate processing.
     *
     * @note Default implementation does nothing to avoid duplicate processing.
     * @note You are responsible for closing file descriptors when done using them.
     */
    virtual void OnReceiveUnixMessage(socket_t fd, const UnixMessage &message)
    {
        (void)fd;
        (void)message;
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