/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_UDP_SERVER_H
#define LMSHAO_NETWORK_UDP_SERVER_H

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "iserver_listener.h"

namespace lmshao::network {

class BaseServer;
class UdpServer final : public Creatable<UdpServer> {
public:
    /**
     * @brief Constructor with IP and port
     * @param listenIp IP address to listen on
     * @param listenPort Port number to listen on
     */
    UdpServer(std::string listenIp, uint16_t listenPort);

    /**
     * @brief Constructor with port only (listen on all interfaces)
     * @param listenPort Port number to listen on
     */
    explicit UdpServer(uint16_t listenPort);

    /**
     * @brief Destructor
     */
    ~UdpServer();

    /**
     * @brief Initialize the UDP server
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Start the UDP server
     * @return true on success, false on failure
     */
    bool Start();

    /**
     * @brief Stop the UDP server
     * @return true on success, false on failure
     */
    bool Stop();

    /**
     * @brief Set the server listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IServerListener> listener);

    /**
     * @brief Get the socket file descriptor
     * @return Socket file descriptor
     */
    socket_t GetSocketFd() const;

    /**
     * @brief Get an idle port number
     * @return Available port number
     */
    static uint16_t GetIdlePort();

    /**
     * @brief Get an idle port pair
     * @return Available port number pair
     */
    static uint16_t GetIdlePortPair();

private:
    std::shared_ptr<BaseServer> impl_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_UDP_SERVER_H
