/**
 * @file log.h
 * @brief Logging System Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LOG_H
#define NETWORK_LOG_H

#include <string>

namespace lmshao::network {
std::string Time();

#define NETWORK_LOG_IMPL(color_start, color_end, fmt, ...)                                                             \
    do {                                                                                                               \
        auto timestamp = lmshao::network::Time();                                                                      \
        printf(color_start "%s - %s:%d - %s: " fmt color_end "\n", timestamp.c_str(), FILENAME_, __LINE__,             \
               __PRETTY_FUNCTION__, ##__VA_ARGS__);                                                                    \
    } while (0)

#define FILENAME_ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef RELEASE

#define NETWORK_LOGD(fmt, ...)
#define NETWORK_LOGW(fmt, ...)
#define NETWORK_LOGE(fmt, ...) NETWORK_LOG_IMPL("\033[0;31m", "\033[0m", fmt, ##__VA_ARGS__)

#else

#define NETWORK_LOGD(fmt, ...) NETWORK_LOG_IMPL("", "", fmt, ##__VA_ARGS__)
#define NETWORK_LOGW(fmt, ...) NETWORK_LOG_IMPL("\033[0;33m", "\033[0m", fmt, ##__VA_ARGS__)
#define NETWORK_LOGE(fmt, ...) NETWORK_LOG_IMPL("\033[0;31m", "\033[0m", fmt, ##__VA_ARGS__)

#endif // RELEASE
} // namespace lmshao::network
#endif // NETWORK_LOG_H