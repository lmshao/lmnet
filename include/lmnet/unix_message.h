/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UNIX_MESSAGE_H
#define LMSHAO_LMNET_UNIX_MESSAGE_H

#include <memory>
#include <vector>

#include "common.h"

namespace lmshao::lmnet {

/**
 * @brief Unix Socket message structure containing both data and file descriptors
 */
struct UnixMessage {
    std::shared_ptr<DataBuffer> data; ///< Message data (can be null for fd-only messages)
    std::vector<int> fds;             ///< File descriptors (can be empty for data-only messages)

    /**
     * @brief Create a data-only message
     */
    static UnixMessage CreateData(std::shared_ptr<DataBuffer> buffer) { return UnixMessage{std::move(buffer), {}}; }

    /**
     * @brief Create a file descriptors-only message
     */
    static UnixMessage CreateFds(std::vector<int> descriptors) { return UnixMessage{nullptr, std::move(descriptors)}; }

    /**
     * @brief Create a mixed message with both data and file descriptors
     */
    static UnixMessage CreateMixed(std::shared_ptr<DataBuffer> buffer, std::vector<int> descriptors)
    {
        return UnixMessage{std::move(buffer), std::move(descriptors)};
    }

    /**
     * @brief Check if message has data
     */
    bool HasData() const { return data && data->Size() > 0; }

    /**
     * @brief Check if message has file descriptors
     */
    bool HasFds() const { return !fds.empty(); }

    /**
     * @brief Check if message is empty
     */
    bool IsEmpty() const { return !HasData() && !HasFds(); }
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_UNIX_MESSAGE_H
