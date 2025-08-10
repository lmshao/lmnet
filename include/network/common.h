/**
 * @file common.h
 * @brief Common Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_COMMON_H
#define NETWORK_COMMON_H

namespace lmshao::network {
#ifdef __linux__
inline constexpr int INVALID_SOCKET = -1;
typedef int socket_t;
#elif defined(_WIN32)
#include <winsock2.h>
typedef SOCKET socket_t;
typedef DWORD pid_t;
#endif

template <typename T>
class Creatable {
public:
    template <typename... Args>
    static std::shared_ptr<T> Create(Args &&...args)
    {
        return std::shared_ptr<T>(new T(std::forward<Args>(args)...));
    }

protected:
    friend T;
};
} // namespace lmshao::network

#endif // NETWORK_COMMON_H