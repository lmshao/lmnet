/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_SESSION_H
#define LMSHAO_LMNET_SESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "unix_message.h"

namespace lmshao::lmnet {

class Session {
public:
    /**
     * @brief Send data buffer
     * @param buffer Data buffer to send
     * @return true on success, false on failure
     */
    virtual bool Send(std::shared_ptr<DataBuffer> buffer) const = 0;

    /**
     * @brief Send string data
     * @param str String data to send
     * @return true on success, false on failure
     */
    virtual bool Send(const std::string &str) const = 0;

    /**
     * @brief Send raw data
     * @param data Pointer to data buffer
     * @param size Size of data
     * @return true on success, false on failure
     */
    virtual bool Send(const void *data, size_t size) const = 0;

#if defined(__linux__) || defined(__APPLE__)
    /**
     * @brief Send file descriptors
     * @param fds File descriptors to send
     * @return true on success, false on failure
     */
    virtual bool SendFds(const std::vector<int> &fds) const { return false; }

    /**
     * @brief Send data buffer with file descriptors
     * @param buffer Data buffer to send
     * @param fds File descriptors to send
     * @return true on success, false on failure
     */
    virtual bool SendWithFds(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds) const { return false; }

    /**
     * @brief Send Unix message (unified interface for data and/or file descriptors)
     * @param message Unix message to send
     * @return true on success, false on failure
     */
    virtual bool SendUnixMessage(const UnixMessage &message) const
    {
        if (message.HasData() && message.HasFds()) {
            return SendWithFds(message.data, message.fds);
        } else if (message.HasData()) {
            return Send(message.data);
        } else if (message.HasFds()) {
            return SendFds(message.fds);
        }
        return false; // Empty message
    }
#endif

    /**
     * @brief Get client information
     * @return Client information string
     */
    virtual std::string ClientInfo() const = 0;

protected:
    Session() = default;
    virtual ~Session() = default;

public:
    std::string host;             ///< Host address
    uint16_t port;                ///< Port number
    socket_t fd = INVALID_SOCKET; ///< Socket file descriptor
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_SESSION_H