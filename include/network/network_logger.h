/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_LOGGER_H
#define LMSHAO_NETWORK_LOGGER_H

#include <coreutils/logger.h>

#include <mutex>

namespace lmshao::network {

// Network module tag for template specialization
struct NetworkModuleTag {};

// Convenience macros for network logging - now use Registry pattern
#define NETWORK_LOGD(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLogger();                                                            \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kDebug)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kDebug, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGI(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLogger();                                                            \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kInfo)) {                                                    \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kInfo, __FILE__,   \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGW(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLogger();                                                            \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kWarn)) {                                                    \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kWarn, __FILE__,   \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGE(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLogger();                                                            \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kError)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kError, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

#define NETWORK_LOGF(fmt, ...)                                                                                         \
    do {                                                                                                               \
        auto &logger = lmshao::network::GetNetworkLogger();                                                            \
        if (logger.ShouldLog(lmshao::coreutils::LogLevel::kFatal)) {                                                   \
            logger.LogWithModuleTag<lmshao::network::NetworkModuleTag>(lmshao::coreutils::LogLevel::kFatal, __FILE__,  \
                                                                       __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);    \
        }                                                                                                              \
    } while (0)

// Inline functions for network logger management - now use Registry pattern
// Initialize Network logger with smart defaults based on build type
inline void InitNetworkLogger(lmshao::coreutils::LogLevel level =
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
                                  lmshao::coreutils::LogLevel::kDebug,
#else
                                  lmshao::coreutils::LogLevel::kWarn,
#endif
                              lmshao::coreutils::LogOutput output = lmshao::coreutils::LogOutput::CONSOLE,
                              const std::string &filename = "")
{
    lmshao::coreutils::LoggerRegistry::InitLogger<NetworkModuleTag>(level, output, filename);
}

inline void SetNetworkLogLevel(lmshao::coreutils::LogLevel level)
{
    auto &logger = lmshao::coreutils::LoggerRegistry::GetLogger<NetworkModuleTag>();
    logger.SetLevel(level);
}

inline lmshao::coreutils::LogLevel GetNetworkLogLevel()
{
    auto &logger = lmshao::coreutils::LoggerRegistry::GetLogger<NetworkModuleTag>();
    return logger.GetLevel();
}

inline void SetNetworkLogOutput(lmshao::coreutils::LogOutput output)
{
    auto &logger = lmshao::coreutils::LoggerRegistry::GetLogger<NetworkModuleTag>();
    logger.SetOutput(output);
}

inline void SetNetworkLogFile(const std::string &filename)
{
    auto &logger = lmshao::coreutils::LoggerRegistry::GetLogger<NetworkModuleTag>();
    logger.SetOutputFile(filename);
}

// Get Network logger without auto-initialization
// This allows explicit initialization in main() to take precedence
inline lmshao::coreutils::Logger &GetNetworkLogger()
{
    return lmshao::coreutils::LoggerRegistry::GetLoggerWithRegistration<NetworkModuleTag>("Network");
}

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_LOGGER_H