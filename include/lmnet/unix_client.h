/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UNIX_CLIENT_H
#define LMSHAO_LMNET_UNIX_CLIENT_H

#include <memory>
#include <string>

#include "common.h"
#include "iclient_listener.h"

#if defined(__linux__) || defined(__APPLE__)

namespace lmshao::lmnet {

class IUnixClient;
class UnixClient : public Creatable<UnixClient> {
public:
    /**
     * @brief Constructor
     * @param socketPath Unix domain socket path
     */
    explicit UnixClient(const std::string &socketPath);

    /**
     * @brief Destructor
     */
    ~UnixClient();

    /**
     * @brief Initialize the Unix client
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Set the client listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IClientListener> listener);

    /**
     * @brief Connect to the Unix domain socket
     * @return true on success, false on failure
     */
    bool Connect();

    /**
     * @brief Send string data
     * @param str String data to send
     * @return true on success, false on failure
     */
    bool Send(const std::string &str);

    /**
     * @brief Send raw data
     * @param data Pointer to data buffer
     * @param len Length of data
     * @return true on success, false on failure
     */
    bool Send(const void *data, size_t len);

    /**
     * @brief Send data buffer
     * @param data Data buffer to send
     * @return true on success, false on failure
     */
    bool Send(std::shared_ptr<DataBuffer> data);

    /**
     * @brief Send file descriptors without additional payload
     * @param fds File descriptors to transfer
     * @return true on success, false on failure
     */
    bool SendFds(const std::vector<int> &fds);

    /**
     * @brief Send data along with file descriptors
     * @param data Data buffer to send
     * @param fds File descriptors to transfer
     * @return true on success, false on failure
     */
    bool SendWithFds(std::shared_ptr<DataBuffer> data, const std::vector<int> &fds);

    /**
     * @brief Close the Unix client
     */
    void Close();

    /**
     * @brief Get the socket file descriptor
     * @return Socket file descriptor
     */
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<IUnixClient> impl_;
};

} // namespace lmshao::lmnet

#endif

#endif // LMSHAO_LMNET_UNIX_CLIENT_H
