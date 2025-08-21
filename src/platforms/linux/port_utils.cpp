/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_utils.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "network_log.h"

namespace lmshao::network {

uint16_t PortUtils::nextPort_ = PortUtils::UDP_PORT_START;

uint16_t PortUtils::GetIdleUdpPort()
{
    int sock;
    struct sockaddr_in addr;

    uint16_t i;
    for (i = nextPort_; i < nextPort_ + 100; i++) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            NETWORK_LOGE("socket creation failed");
            return 0;
        }

        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(i);

        if (bind(sock, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
            close(sock);
            continue;
        }

        close(sock);
        break;
    }

    if (i == nextPort_ + 100) {
        NETWORK_LOGE("Can't find idle port");
        return 0;
    }

    nextPort_ = i + 1;
    return i;
}

uint16_t PortUtils::GetIdleUdpPortPair()
{
    uint16_t firstPort;
    uint16_t secondPort;
    firstPort = GetIdleUdpPort();

    if (firstPort == 0) {
        return 0;
    }

    while (true) {
        secondPort = GetIdleUdpPort();
        if (secondPort == 0) {
            return 0;
        }

        if (firstPort + 1 == secondPort) {
            return firstPort;
        } else {
            firstPort = secondPort;
        }
    }
}

} // namespace lmshao::network
