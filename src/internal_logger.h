/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_INTERNAL_LOGGER_H
#define LMSHAO_NETWORK_INTERNAL_LOGGER_H

#include "network/network_logger.h"

namespace lmshao::network {

/**
 * @brief Get Network logger with automatic initialization
 * This version ensures the logger is properly initialized before use.
 * Used internally by Network modules.
 */
inline lmshao::coreutils::Logger &GetNetworkLoggerWithAutoInit()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        lmshao::coreutils::LoggerRegistry::RegisterModule<NetworkModuleTag>("Network");
        InitNetworkLogger();
    });
    return lmshao::coreutils::LoggerRegistry::GetLogger<NetworkModuleTag>();
}

// Internal Network logging macros with auto-initialization and module tagging
#define NETWORK_LOGD(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLoggerWithAutoInit();                                                \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kDebug)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kDebug, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGI(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLoggerWithAutoInit();                                                \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kInfo)) {                                                    \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kInfo, __FILE__,   \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGW(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLoggerWithAutoInit();                                                \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kWarn)) {                                                    \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kWarn, __FILE__,   \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGE(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLoggerWithAutoInit();                                                \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kError)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kError, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGF(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLoggerWithAutoInit();                                                \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kFatal)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kFatal, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_INTERNAL_LOGGER_H
