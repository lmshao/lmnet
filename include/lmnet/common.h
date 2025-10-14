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

#include <lmcore/data_buffer.h>
#include <lmcore/logger.h>

namespace lmshao::lmnet {
#if defined(__linux__) || defined(__APPLE__)
inline constexpr int INVALID_SOCKET = -1;
using socket_t = int;
#elif _WIN32
using socket_t = SOCKET;
using pid_t = DWORD;
#endif

using DataBuffer = lmshao::lmcore::DataBuffer;

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_COMMON_H