/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include "internal_logger.h"
#include "lmnet/network_utils.h"

namespace lmshao::lmnet {

std::vector<NetworkInterface> NetworkUtils::GetAllInterfaces()
{
    std::vector<NetworkInterface> interfaces;
    struct ifaddrs *ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1) {
        LMNET_LOGE("getifaddrs failed");
        return interfaces;
    }

    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        NetworkInterface iface;
        iface.name = ifa->ifa_name;
        iface.isUp = (ifa->ifa_flags & IFF_UP) != 0;
        iface.isLoopback = (ifa->ifa_flags & IFF_LOOPBACK) != 0;

        // IPv4
        if (ifa->ifa_addr->sa_family == AF_INET) {
            auto *sa = reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr);
            char buf[INET_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET, &(sa->sin_addr), buf, sizeof(buf)) != nullptr) {
                iface.ipv4 = buf;
                interfaces.push_back(iface);
            }
        }
        // IPv6
        else if (ifa->ifa_addr->sa_family == AF_INET6) {
            auto *sa = reinterpret_cast<struct sockaddr_in6 *>(ifa->ifa_addr);
            char buf[INET6_ADDRSTRLEN] = {0};
            if (inet_ntop(AF_INET6, &(sa->sin6_addr), buf, sizeof(buf)) != nullptr) {
                iface.ipv6 = buf;
                interfaces.push_back(iface);
            }
        }
    }

    freeifaddrs(ifaddr);
    return interfaces;
}

} // namespace lmshao::lmnet
