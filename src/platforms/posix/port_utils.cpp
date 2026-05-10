/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_utils.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <mutex>

#include "internal_logger.h"

namespace lmshao::lmnet {

namespace {
std::mutex g_portMutex;

uint16_t GetIdleUdpPortUnlocked(uint16_t &nextPort)
{
    int sock;
    struct sockaddr_in addr;

    uint16_t i;
    for (i = nextPort; i < nextPort + 100; i++) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            LMNET_LOGE("socket creation failed");
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

    if (i == nextPort + 100) {
        LMNET_LOGE("Can't find idle port");
        return 0;
    }

    nextPort = i + 1;
    return i;
}
} // namespace

uint16_t PortUtils::nextPort_ = PortUtils::UDP_PORT_START;

uint16_t PortUtils::GetIdleUdpPort()
{
    std::lock_guard<std::mutex> lock(g_portMutex);
    return GetIdleUdpPortUnlocked(nextPort_);
}

uint16_t PortUtils::GetIdleUdpPortPair()
{
    std::lock_guard<std::mutex> lock(g_portMutex);

    uint16_t firstPort;
    uint16_t secondPort;
    firstPort = GetIdleUdpPortUnlocked(nextPort_);

    if (firstPort == 0) {
        return 0;
    }

    while (true) {
        secondPort = GetIdleUdpPortUnlocked(nextPort_);
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

} // namespace lmshao::lmnet
