//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_LOG_H
#define NETWORK_LOG_H

#include <string>

namespace Network {
std::string Time();

#define NETWORK_LOG_IMPL(color_start, color_end, fmt, ...)                                                             \
    do {                                                                                                               \
        auto timestamp = Network::Time();                                                                              \
        printf(color_start "%s - %s:%d - %s: " fmt color_end "\n", timestamp.c_str(), FILENAME_, __LINE__,             \
               __PRETTY_FUNCTION__, ##__VA_ARGS__);                                                                    \
    } while (0)

#define FILENAME_ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef RELEASE

#define NETWORK_LOGD(fmt, ...)
#define NETWORK_LOGW(fmt, ...)

#else

#define NETWORK_LOGD(fmt, ...) NETWORK_LOG_IMPL("", "", fmt, ##__VA_ARGS__)
#define NETWORK_LOGW(fmt, ...) NETWORK_LOG_IMPL("\033[0;32m", "\033[0m", fmt, ##__VA_ARGS__)
#define NETWORK_LOGE(fmt, ...) NETWORK_LOG_IMPL("\033[0;33m", "\033[0m", fmt, ##__VA_ARGS__)

#endif // RELEASE
} // namespace Network
#endif // NETWORK_LOG_H