//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_LOG_H
#define NETWORK_LOG_H

#include <string>

namespace Network {

std::string Time();

#define FILENAME_ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef RELEASE

#define NETWORK_LOGD(fmt, ...)
#define NETWORK_LOGW(fmt, ...)

#else

#define NETWORK_LOGD(fmt, ...)                                                                                         \
    do {                                                                                                               \
        printf("%s - %s:%d - %s: " fmt "\n", Network::Time().c_str(), FILENAME_, __LINE__, __PRETTY_FUNCTION__,        \
               ##__VA_ARGS__);                                                                                         \
    } while (0);

#define NETWORK_LOGW(fmt, ...)                                                                                         \
    do {                                                                                                               \
        printf("\033[0;33m%s - %s:%d - %s: " fmt "\033[0m\n", Network::Time().c_str(), FILENAME_, __LINE__,            \
               __PRETTY_FUNCTION__, ##__VA_ARGS__);                                                                    \
    } while (0);

#endif // RELEASE

#define NETWORK_LOGE(fmt, ...)                                                                                         \
    do {                                                                                                               \
        printf("\033[0;31m%s - %s:%d - %s: " fmt "\033[0m\n", Network::Time().c_str(), FILENAME_, __LINE__,            \
               __PRETTY_FUNCTION__, ##__VA_ARGS__);                                                                    \
    } while (0);

} // namespace Network
#endif // NETWORK_LOG_H