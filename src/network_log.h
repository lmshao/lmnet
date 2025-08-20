/**
 * @file network_log.h
 * @brief Logging System Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_LOG_H
#define NETWORK_LOG_H

#include <chrono>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace lmshao::network {

#ifdef _WIN32
#define COLOR_RED ""
#define COLOR_YELLOW ""
#define COLOR_RESET ""
#define FUNC_NAME_ __FUNCTION__
#define FILENAME_ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#define NETWORK_LOG_TIME_STR                                                                                           \
    ([]() -> const char * {                                                                                            \
        using namespace std::chrono;                                                                                   \
        static char buf[32];                                                                                           \
        auto now = system_clock::now();                                                                                \
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;                                          \
        std::time_t t = system_clock::to_time_t(now);                                                                  \
        std::tm tm;                                                                                                    \
        localtime_s(&tm, &t);                                                                                          \
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);                                                     \
        std::snprintf(buf + 19, sizeof(buf) - 19, ".%03d", static_cast<int>(ms.count()));                              \
        return buf;                                                                                                    \
    })()
#else
#define COLOR_RED "\033[31m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_RESET "\033[0m"
#define FUNC_NAME_ __FUNCTION__
#define FILENAME_ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)
#define NETWORK_LOG_TIME_STR                                                                                           \
    ([]() -> const char * {                                                                                            \
        using namespace std::chrono;                                                                                   \
        static char buf[32];                                                                                           \
        auto now = system_clock::now();                                                                                \
        auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;                                          \
        std::time_t t = system_clock::to_time_t(now);                                                                  \
        std::tm tm = *std::localtime(&t);                                                                              \
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);                                                     \
        std::snprintf(buf + 19, sizeof(buf) - 19, ".%03d", static_cast<int>(ms.count()));                              \
        return buf;                                                                                                    \
    })()
#endif

#ifdef DEBUG
#define NETWORK_LOGD(fmt, ...)                                                                                         \
    printf("%s [DEBUG] %s:%d  %s() " fmt "\n", NETWORK_LOG_TIME_STR, FILENAME_, __LINE__, FUNC_NAME_, ##__VA_ARGS__)
#define NETWORK_LOGW(fmt, ...)                                                                                         \
    printf(COLOR_YELLOW "%s [WARN] %s:%d %s() " fmt "\n" COLOR_RESET, NETWORK_LOG_TIME_STR, FILENAME_, __LINE__,       \
           FUNC_NAME_, ##__VA_ARGS__)
#define NETWORK_LOGE(fmt, ...)                                                                                         \
    printf(COLOR_RED "%s [ERROR] %s:%d %s() " fmt "\n" COLOR_RESET, NETWORK_LOG_TIME_STR, FILENAME_, __LINE__,         \
           FUNC_NAME_, ##__VA_ARGS__)
#else
#define NETWORK_LOGD(fmt, ...)
#define NETWORK_LOGW(fmt, ...)
#define NETWORK_LOGE(fmt, ...)                                                                                         \
    printf(COLOR_RED "%s [ERROR] %s:%d %s() " fmt "\n" COLOR_RESET, NETWORK_LOG_TIME_STR, FILENAME_, __LINE__,         \
           FUNC_NAME_, ##__VA_ARGS__)
#endif

} // namespace lmshao::network

#endif // NETWORK_LOG_H
