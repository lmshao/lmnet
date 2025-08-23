/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_SESSION_H
#define LMSHAO_NETWORK_SESSION_H

#include <coreutils/data_buffer.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "common.h"

namespace lmshao::network {
using namespace lmshao::coreutils;

class Session {
public:
    std::string host;             ///< Host address
    uint16_t port;                ///< Port number
    socket_t fd = INVALID_SOCKET; ///< Socket file descriptor

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

    /**
     * @brief Get client information
     * @return Client information string
     */
    virtual std::string ClientInfo() const = 0;

protected:
    Session() = default;
    virtual ~Session() = default;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_SESSION_H