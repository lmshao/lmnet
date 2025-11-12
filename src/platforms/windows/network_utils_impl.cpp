/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Include order matters on Windows:
// 1) winsock2.h before windows.h to avoid conflicts with winsock.h
// 2) windows.h before iphlpapi.h so Windows types (e.g., DWORD) are defined
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
// clang-format on
#include "internal_logger.h"
#include "lmnet/network_utils.h"

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace lmshao::lmnet {

std::vector<NetworkInterface> NetworkUtils::GetAllInterfaces()
{
    std::vector<NetworkInterface> interfaces;

    // Allocate buffer for adapter addresses
    ULONG bufferSize = 15000;
    PIP_ADAPTER_ADDRESSES pAddresses = nullptr;

    // Try to allocate buffer
    pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(malloc(bufferSize));
    if (pAddresses == nullptr) {
        LMNET_LOGE("Failed to allocate memory for adapter addresses");
        return interfaces;
    }

    // Get adapter addresses
    DWORD ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &bufferSize);

    if (ret == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(malloc(bufferSize));
        if (pAddresses == nullptr) {
            LMNET_LOGE("Failed to reallocate memory for adapter addresses");
            return interfaces;
        }
        ret = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &bufferSize);
    }

    if (ret != NO_ERROR) {
        LMNET_LOGE("GetAdaptersAddresses failed with error: %lu", ret);
        free(pAddresses);
        return interfaces;
    }

    // Iterate through adapters
    for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != nullptr; pCurr = pCurr->Next) {
        // Check if interface is up
        if (pCurr->OperStatus != IfOperStatusUp) {
            continue;
        }

        // Iterate through unicast addresses
        for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress; pUnicast != nullptr;
             pUnicast = pUnicast->Next) {
            auto sa = pUnicast->Address.lpSockaddr;

            NetworkInterface iface;

            // Convert adapter name from wide string to multi-byte string
            int nameLen = WideCharToMultiByte(CP_UTF8, 0, pCurr->FriendlyName, -1, nullptr, 0, nullptr, nullptr);
            if (nameLen > 0) {
                iface.name.resize(nameLen - 1);
                WideCharToMultiByte(CP_UTF8, 0, pCurr->FriendlyName, -1, &iface.name[0], nameLen, nullptr, nullptr);
            }

            iface.isUp = (pCurr->OperStatus == IfOperStatusUp);
            iface.isLoopback = (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK);

            // IPv4
            if (sa->sa_family == AF_INET) {
                char buf[INET_ADDRSTRLEN] = {0};
                auto *sin = reinterpret_cast<struct sockaddr_in *>(sa);
                if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf)) != nullptr) {
                    iface.ipv4 = buf;
                    interfaces.push_back(iface);
                }
            }
            // IPv6
            else if (sa->sa_family == AF_INET6) {
                char buf[INET6_ADDRSTRLEN] = {0};
                auto *sin6 = reinterpret_cast<struct sockaddr_in6 *>(sa);
                if (inet_ntop(AF_INET6, &sin6->sin6_addr, buf, sizeof(buf)) != nullptr) {
                    std::string ip = buf;
                    // Append scope id for link-local IPv6 addresses to make them usable, e.g., fe80::...%12
                    // Prefer sockaddr scope id; if zero, fallback to adapter's Ipv6IfIndex
                    ULONG scope = sin6->sin6_scope_id;
                    // On Windows, link-local addresses typically require a non-zero scope id
                    // Detect link-local by prefix fe80::/10; IN6_IS_ADDR_LINKLOCAL is available on Windows headers
#ifdef IN6_IS_ADDR_LINKLOCAL
                    bool is_link_local = IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) != 0;
#else
                    bool is_link_local = (ip.rfind("fe80", 0) == 0);
#endif
                    if (is_link_local) {
                        if (scope == 0) {
                            scope = pCurr->Ipv6IfIndex; // adapter index for IPv6
                        }
                        if (scope != 0) {
                            ip += "%" + std::to_string(scope);
                        }
                    }
                    iface.ipv6 = ip;
                    interfaces.push_back(iface);
                }
            }
        }
    }

    free(pAddresses);
    return interfaces;
}

} // namespace lmshao::lmnet
