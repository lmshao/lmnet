/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_TCP_CLIENT_H
#define LMSHAO_NETWORK_TCP_CLIENT_H

#include <coreutils/data_buffer.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "iclient_listener.h"

namespace lmshao::network {

class ITcpClient;
class TcpClient final : public Creatable<TcpClient> {
public:
    /**
     * @brief Constructor
     * @param remoteIp Remote IP address
     * @param remotePort Remote port number
     * @param localIp Local IP address (optional)
     * @param localPort Local port number (optional)
     */
    TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    /**
     * @brief Destructor
     */
    ~TcpClient();

    /**
     * @brief Initialize the TCP client
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Set the client listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IClientListener> listener);

    /**
     * @brief Connect to the remote server
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
     * @brief Close the TCP client
     */
    void Close();

    /**
     * @brief Get the socket file descriptor
     * @return Socket file descriptor
     */
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<ITcpClient> impl_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_TCP_CLIENT_H