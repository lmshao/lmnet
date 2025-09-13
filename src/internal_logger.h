/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_INTERNAL_LOGGER_H
#define LMSHAO_LMNET_INTERNAL_LOGGER_H

#include "lmnet/lmnet_logger.h"

namespace lmshao::lmnet {

/**
 * @brief Get Lmnet logger with automatic initialization
 *
 * Used internally by Lmnet modules.
 * Ensures the logger is initialized before first use.
 */
inline lmshao::lmcore::Logger &GetLmnetLoggerWithAutoInit()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        lmshao::lmcore::LoggerRegistry::RegisterModule<LmnetModuleTag>("Lmnet");
        InitLmnetLogger();
    });
    return lmshao::lmcore::LoggerRegistry::GetLogger<LmnetModuleTag>();
}

// Internal Lmnet logging macros with auto-initialization and module tagging
#define LMNET_LOGD(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = lmshao::lmnet::GetLmnetLoggerWithAutoInit();                                                    \
        if (logger.ShouldLog(lmshao::lmcore::LogLevel::kDebug)) {                                                      \
            logger.LogWithModuleTag<lmshao::lmnet::LmnetModuleTag>(lmshao::lmcore::LogLevel::kDebug, __FILE__,         \
                                                                   __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);        \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGI(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = lmshao::lmnet::GetLmnetLoggerWithAutoInit();                                                    \
        if (logger.ShouldLog(lmshao::lmcore::LogLevel::kInfo)) {                                                       \
            logger.LogWithModuleTag<lmshao::lmnet::LmnetModuleTag>(lmshao::lmcore::LogLevel::kInfo, __FILE__,          \
                                                                   __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);        \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGW(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = lmshao::lmnet::GetLmnetLoggerWithAutoInit();                                                    \
        if (logger.ShouldLog(lmshao::lmcore::LogLevel::kWarn)) {                                                       \
            logger.LogWithModuleTag<lmshao::lmnet::LmnetModuleTag>(lmshao::lmcore::LogLevel::kWarn, __FILE__,          \
                                                                   __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);        \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGE(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = lmshao::lmnet::GetLmnetLoggerWithAutoInit();                                                    \
        if (logger.ShouldLog(lmshao::lmcore::LogLevel::kError)) {                                                      \
            logger.LogWithModuleTag<lmshao::lmnet::LmnetModuleTag>(lmshao::lmcore::LogLevel::kError, __FILE__,         \
                                                                   __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);        \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGF(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = lmshao::lmnet::GetLmnetLoggerWithAutoInit();                                                    \
        if (logger.ShouldLog(lmshao::lmcore::LogLevel::kFatal)) {                                                      \
            logger.LogWithModuleTag<lmshao::lmnet::LmnetModuleTag>(lmshao::lmcore::LogLevel::kFatal, __FILE__,         \
                                                                   __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__);        \
        }                                                                                                              \
    } while (0)

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_INTERNAL_LOGGER_H
