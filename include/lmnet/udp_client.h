/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UDP_CLIENT_H
#define LMSHAO_LMNET_UDP_CLIENT_H

#include <lmcore/data_buffer.h>

#include <cstdint>
#include <memory>
#include <string>

#include "common.h"
#include "iclient_listener.h"

namespace lmshao::lmnet {

class IUdpClient;
class UdpClient final : public Creatable<UdpClient> {
public:
    /**
     * @brief Constructor
     * @param remoteIp Remote IP address
     * @param remotePort Remote port number
     * @param localIp Local IP address (optional)
     * @param localPort Local port number (optional)
     */
    UdpClient(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    /**
     * @brief Destructor
     */
    ~UdpClient();

    /**
     * @brief Initialize the UDP client
     * @return true on success, false on failure
     */
    bool Init();

    /**
     * @brief Set the client listener for receiving callbacks
     * @param listener Listener for handling events
     */
    void SetListener(std::shared_ptr<IClientListener> listener);

    /**
     * @brief Enable UDP broadcast functionality
     * @return true on success, false on failure
     */
    bool EnableBroadcast();

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
     * @brief Close the UDP client
     */
    void Close();

    /**
     * @brief Get the socket file descriptor
     * @return Socket file descriptor
     */
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<IUdpClient> impl_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_UDP_CLIENT_H