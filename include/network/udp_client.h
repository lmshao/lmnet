/**
 * @file udp_client.h
 * @brief UDP Client Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_UDP_CLIENT_H
#define NETWORK_UDP_CLIENT_H

#include <netinet/in.h>

#include <cstdint>
#include <memory>
#include <string>

#include "data_buffer.h"
#include "iclient_listener.h"
#include "task_queue.h"

namespace lmshao::network {

class EventHandler;

class UdpClient final : public std::enable_shared_from_this<UdpClient> {
    friend class UdpClientHandler;
    const int INVALID_SOCKET = -1;

public:
    template <typename... Args>
    static std::shared_ptr<UdpClient> Create(Args &&...args)
    {
        return std::shared_ptr<UdpClient>(new UdpClient(std::forward<Args>(args)...));
    }

    ~UdpClient();

    bool Init();
    void SetListener(std::shared_ptr<IClientListener> listener) { listener_ = listener; }

    bool Send(const std::string &str);
    bool Send(const void *data, size_t len);
    bool Send(std::shared_ptr<DataBuffer> data);

    void Close();

    int GetSocketFd() const { return socket_; }

protected:
    UdpClient(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    void HandleReceive(int fd);
    void HandleConnectionClose(int fd, bool isError, const std::string &reason);

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    int socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<EventHandler> clientHandler_;
};

} // namespace lmshao::network

#endif // NETWORK_UDP_CLIENT_H