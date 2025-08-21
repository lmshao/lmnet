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
    TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);
    ~TcpClient();

    bool Init();
    void SetListener(std::shared_ptr<IClientListener> listener);
    bool Connect();
    bool Send(const std::string &str);
    bool Send(const void *data, size_t len);
    bool Send(std::shared_ptr<DataBuffer> data);
    void Close();
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<class ITcpClient> impl_;
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_TCP_CLIENT_H