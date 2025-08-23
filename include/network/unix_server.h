/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_UNIX_SERVER_H
#define LMSHAO_NETWORK_UNIX_SERVER_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <memory>
#include <string>

#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class BaseServer;

class UnixServer : public Creatable<UnixServer> {
public:
    /**
     * @brief Constructor
     * @param socketPath Unix domain socket path
     */
    explicit UnixServer(const std::string &socketPath);

    /**
     * @brief Destructor
     */
    ~UnixServer();

    /**
     * @brief Initialize the Unix server
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Set the server listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IServerListener> listener);

    /**
     * @brief Start the Unix server
     * @return true on success, false on failure
     */
    bool Start();

    /**
     * @brief Stop the Unix server
     * @return true on success, false on failure
     */
    bool Stop();

    /**
     * @brief Get the socket file descriptor
     * @return Socket file descriptor
     */
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<BaseServer> impl_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_UNIX_SERVER_H
