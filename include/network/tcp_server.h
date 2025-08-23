/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_TCP_SERVER_H
#define LMSHAO_NETWORK_TCP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class BaseServer;
class TcpServer final : public Creatable<TcpServer> {
public:
    /**
     * @brief Constructor with IP and port
     * @param listenIp IP address to listen on
     * @param listenPort Port number to listen on
     */
    TcpServer(std::string listenIp, uint16_t listenPort);

    /**
     * @brief Constructor with port only (listen on all interfaces)
     * @param listenPort Port number to listen on
     */
    explicit TcpServer(uint16_t listenPort);

    /**
     * @brief Destructor
     */
    ~TcpServer();

    /**
     * @brief Initialize the TCP server
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Set the server listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IServerListener> listener);

    /**
     * @brief Start the TCP server
     * @return true on success, false on failure
     */
    bool Start();

    /**
     * @brief Stop the TCP server
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

#endif // LMSHAO_NETWORK_TCP_SERVER_H