/**
 * @file port_utils.cpp
 * @brief Port Discovery Utilities Implementation for Windows Platform
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_utils.h"

#include <mutex>

#include "common.h"

namespace lmshao {
namespace network {

uint16_t PortUtils::nextPort_ = PortUtils::UDP_PORT_START;

static std::mutex portMutex;

uint16_t PortUtils::GetIdleUdpPort()
{
    std::lock_guard<std::mutex> lock(portMutex);

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }

    for (int attempts = 0; attempts < 1000; ++attempts) {
        uint16_t port = nextPort_++;
        if (nextPort_ > 65000) {
            nextPort_ = UDP_PORT_START;
        }

        SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET) {
            continue;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (bind(sock, (sockaddr *)&addr, sizeof(addr)) == 0) {
            closesocket(sock);
            WSACleanup();
            return port;
        }

        closesocket(sock);
    }

    WSACleanup();
    return 0;
}

uint16_t PortUtils::GetIdleUdpPortPair()
{
    std::lock_guard<std::mutex> lock(portMutex);

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return 0;
    }

    for (int attempts = 0; attempts < 1000; ++attempts) {
        uint16_t port = nextPort_;
        nextPort_ += 2;
        if (nextPort_ > 65000) {
            nextPort_ = UDP_PORT_START;
        }

        // Check if both ports are available
        SOCKET sock1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        SOCKET sock2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

        if (sock1 == INVALID_SOCKET || sock2 == INVALID_SOCKET) {
            if (sock1 != INVALID_SOCKET)
                closesocket(sock1);
            if (sock2 != INVALID_SOCKET)
                closesocket(sock2);
            continue;
        }

        sockaddr_in addr1{}, addr2{};
        addr1.sin_family = addr2.sin_family = AF_INET;
        addr1.sin_addr.s_addr = addr2.sin_addr.s_addr = htonl(INADDR_ANY);
        addr1.sin_port = htons(port);
        addr2.sin_port = htons(port + 1);

        if (bind(sock1, (sockaddr *)&addr1, sizeof(addr1)) == 0 &&
            bind(sock2, (sockaddr *)&addr2, sizeof(addr2)) == 0) {
            closesocket(sock1);
            closesocket(sock2);
            WSACleanup();
            return port;
        }

        closesocket(sock1);
        closesocket(sock2);
    }

    WSACleanup();
    return 0;
}

} // namespace network
} // namespace lmshao
