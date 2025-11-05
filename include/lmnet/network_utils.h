/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_NETWORK_UTILS_H
#define LMSHAO_LMNET_NETWORK_UTILS_H

#include <string>
#include <vector>

namespace lmshao::lmnet {

/**
 * @brief Network interface information
 */
struct NetworkInterface {
    std::string name; // Interface name (e.g., "eth0", "en0", "Wi-Fi")
    std::string ipv4; // IPv4 address (e.g., "192.168.1.100")
    std::string ipv6; // IPv6 address (e.g., "fe80::1")
    bool isLoopback;  // True if this is a loopback interface
    bool isUp;        // True if the interface is up

    NetworkInterface() : isLoopback(false), isUp(false) {}
};

/**
 * @brief Network utility functions for interface enumeration
 */
class NetworkUtils {
public:
    /**
     * @brief Get all network interfaces with IPv4 addresses
     * @return Vector of network interfaces
     */
    static std::vector<NetworkInterface> GetAllInterfaces();

    /**
     * @brief Get all local IPv4 addresses (excluding loopback)
     * @return Vector of IPv4 address strings
     */
    static std::vector<std::string> GetLocalIPv4Addresses();

    /**
     * @brief Get all local IPv4 addresses (including loopback)
     * @return Vector of IPv4 address strings
     */
    static std::vector<std::string> GetAllIPv4Addresses();

    /**
     * @brief Check if an IP address is a loopback address
     * @param ip IP address to check
     * @return True if the IP is a loopback address
     */
    static bool IsLoopback(const std::string &ip);
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_NETWORK_UTILS_H
