/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_COMMON_H
#define LMSHAO_LMNET_COMMON_H

#include <memory>
#ifdef _WIN32
#include <winsock2.h>
#endif

namespace lmshao::lmnet {
#ifdef __linux__
inline constexpr int INVALID_SOCKET = -1;
using socket_t = int;
#elif _WIN32
using socket_t = SOCKET;
using pid_t = DWORD;
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
} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_COMMON_H