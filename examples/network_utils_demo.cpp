/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <lmnet/network_utils.h>

#include <iostream>

using namespace lmshao::lmnet;

int main()
{
    std::cout << "=== Network Interface Enumeration Demo ===\n\n";

    // Get all interfaces
    auto interfaces = NetworkUtils::GetAllInterfaces();
    std::cout << "All network interfaces (" << interfaces.size() << "):\n";
    for (const auto &iface : interfaces) {
        std::cout << "  Interface: " << iface.name << "\n";
        std::cout << "    IPv4: " << iface.ipv4 << "\n";
        if (!iface.ipv6.empty()) {
            std::cout << "    IPv6: " << iface.ipv6 << "\n";
        }
        std::cout << "    Loopback: " << (iface.isLoopback ? "Yes" : "No") << "\n";
        std::cout << "    Up: " << (iface.isUp ? "Yes" : "No") << "\n";
        std::cout << "\n";
    }

    // Get local IPv4 addresses (excluding loopback)
    auto localIps = NetworkUtils::GetLocalIPv4Addresses();
    std::cout << "\nLocal IPv4 addresses (excluding loopback):\n";
    for (const auto &ip : localIps) {
        std::cout << "  " << ip << "\n";
    }

    // Get all IPv4 addresses (including loopback)
    auto allIps = NetworkUtils::GetAllIPv4Addresses();
    std::cout << "\nAll IPv4 addresses (including loopback):\n";
    for (const auto &ip : allIps) {
        std::cout << "  " << ip << " " << (NetworkUtils::IsLoopback(ip) ? "(loopback)" : "") << "\n";
    }

    return 0;
}
