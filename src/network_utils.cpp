/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "lmnet/network_utils.h"

#include <algorithm>

namespace lmshao::lmnet {

std::vector<std::string> NetworkUtils::GetLocalIPv4Addresses()
{
    std::vector<std::string> addresses;
    auto interfaces = GetAllInterfaces();

    for (const auto &iface : interfaces) {
        if (!iface.ipv4.empty() && !iface.isLoopback) {
            addresses.push_back(iface.ipv4);
        }
    }

    // Remove duplicates
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());

    return addresses;
}

std::vector<std::string> NetworkUtils::GetAllIPv4Addresses()
{
    std::vector<std::string> addresses;
    auto interfaces = GetAllInterfaces();

    for (const auto &iface : interfaces) {
        if (!iface.ipv4.empty()) {
            addresses.push_back(iface.ipv4);
        }
    }

    // Remove duplicates
    std::sort(addresses.begin(), addresses.end());
    addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());

    return addresses;
}

bool NetworkUtils::IsLoopback(const std::string &ip)
{
    // Check for IPv4 loopback (127.0.0.0/8)
    if (ip.substr(0, 4) == "127.") {
        return true;
    }
    // Check for IPv6 loopback
    if (ip == "::1" || ip == "0:0:0:0:0:0:0:1") {
        return true;
    }
    return false;
}

} // namespace lmshao::lmnet
