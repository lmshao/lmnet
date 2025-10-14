/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UNIX_SOCKET_UTILS_H
#define LMSHAO_LMNET_UNIX_SOCKET_UTILS_H

#include <functional>
#include <memory>
#include <vector>

#include "internal_logger.h"
#include "lmnet/common.h"
#include "lmnet/iclient_listener.h"
#include "lmnet/iserver_listener.h"
#include "lmnet/unix_message.h"

namespace lmshao::lmnet {

/**
 * @brief Unix Socket utility functions for fd passing and message processing
 */
class UnixSocketUtils {
public:
    /**
     * @brief Duplicate file descriptors safely
     * @param fds Original file descriptors
     * @return Vector of duplicated fds, or empty if failed (all duplicated fds are cleaned up on failure)
     */
    static std::vector<int> DuplicateFds(const std::vector<int> &fds)
    {
        std::vector<int> duplicatedFds;
        duplicatedFds.reserve(fds.size());

        for (int fd : fds) {
            if (fd < 0) {
                LMNET_LOGE("Invalid file descriptor: %d", fd);
                continue;
            }
            int dupFd = ::dup(fd);
            if (dupFd == -1) {
                LMNET_LOGE("Failed to duplicate file descriptor %d: %s", fd, strerror(errno));
                // Clean up already duplicated descriptors
                for (int i : duplicatedFds) {
                    ::close(i);
                }
                return {}; // Return empty vector on failure
            }
            duplicatedFds.push_back(dupFd);
        }

        return duplicatedFds;
    }

    /**
     * @brief Clean up file descriptors
     * @param fds File descriptors to close
     */
    static void CleanupFds(const std::vector<int> &fds)
    {
        for (int fd : fds) {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    }

    /**
     * @brief Create cleanup callback for duplicated fds
     * @param duplicatedFds File descriptors to clean up after operation
     * @param operation_name Name of operation for logging
     * @return Callback function for IoUringManager
     */
    static std::function<void(int, int)> CreateCleanupCallback(std::vector<int> duplicatedFds,
                                                               const char *operation_name)
    {
        return [duplicatedFds = std::move(duplicatedFds), operation_name](int, int res) {
            // Clean up duplicated file descriptors after sending
            CleanupFds(duplicatedFds);
            if (res < 0) {
                LMNET_LOGE("%s failed: %s", operation_name, strerror(-res));
            }
        };
    }

    /**
     * @brief Process received Unix message for client listener
     * @param listener Client listener
     * @param fd Socket file descriptor
     * @param buffer Received data buffer
     * @param fds Received file descriptors
     */
    static void ProcessClientMessage(std::shared_ptr<IClientListener> listener, socket_t fd,
                                     std::shared_ptr<DataBuffer> buffer, std::vector<int> fds)
    {
        if (!listener) {
            CleanupFds(fds);
            return;
        }

        // Check if this is just a placeholder for fd-only message
        bool isPlaceholder = !fds.empty() && buffer && buffer->Size() == 1 && buffer->Data()[0] == 0;

        // Call traditional callback for backward compatibility (data only)
        if (!isPlaceholder && buffer && buffer->Size() > 0) {
            listener->OnReceive(fd, buffer);
        }

        // Call unified callback with FDs (handles both data and FDs)
        if (!isPlaceholder || !fds.empty()) {
            UnixMessage message;
            message.data = isPlaceholder ? nullptr : buffer;
            message.fds = std::move(fds); // Transfer ownership to the message
            listener->OnReceiveUnixMessage(fd, message);
        } else {
            // No FDs to pass, clean up if any
            CleanupFds(fds);
        }
    }

    /**
     * @brief Process received Unix message for server listener
     * @param listener Server listener
     * @param session Session that received the message
     * @param buffer Received data buffer
     * @param fds Received file descriptors
     */
    static void ProcessServerMessage(std::shared_ptr<IServerListener> listener, std::shared_ptr<Session> session,
                                     std::shared_ptr<DataBuffer> buffer, std::vector<int> fds)
    {
        if (!listener) {
            CleanupFds(fds);
            return;
        }

        // Check if this is just a placeholder for fd-only message
        bool isPlaceholder = !fds.empty() && buffer && buffer->Size() == 1 && buffer->Data()[0] == 0;

        // Call traditional callback for backward compatibility (data only)
        if (!isPlaceholder && buffer && buffer->Size() > 0) {
            listener->OnReceive(session, buffer);
        }

        // Call unified callback with FDs (handles both data and FDs)
        if (!isPlaceholder || !fds.empty()) {
            UnixMessage message;
            message.data = isPlaceholder ? nullptr : buffer;
            message.fds = std::move(fds); // Transfer ownership to the message
            listener->OnReceiveUnixMessage(session, message);
        } else {
            // No FDs to pass, clean up if any
            CleanupFds(fds);
        }
    }
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_UNIX_SOCKET_UTILS_H
