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
inline lmcore::Logger &GetLmnetLoggerWithAutoInit()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        lmcore::LoggerRegistry::RegisterModule<LmnetModuleTag>("LMNet");
        InitLmnetLogger();
    });
    return lmcore::LoggerRegistry::GetLogger<LmnetModuleTag>();
}

// Internal Lmnet logging macros with auto-initialization and module tagging
#define LMNET_LOGD(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = GetLmnetLoggerWithAutoInit();                                                                   \
        if (logger.ShouldLog(lmcore::LogLevel::kDebug)) {                                                              \
            logger.LogWithModuleTag<LmnetModuleTag>(lmcore::LogLevel::kDebug, __FILE__, __LINE__, __FUNCTION__, fmt,   \
                                                    ##__VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGI(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = GetLmnetLoggerWithAutoInit();                                                                   \
        if (logger.ShouldLog(lmcore::LogLevel::kInfo)) {                                                               \
            logger.LogWithModuleTag<LmnetModuleTag>(lmcore::LogLevel::kInfo, __FILE__, __LINE__, __FUNCTION__, fmt,    \
                                                    ##__VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGW(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = GetLmnetLoggerWithAutoInit();                                                                   \
        if (logger.ShouldLog(lmcore::LogLevel::kWarn)) {                                                               \
            logger.LogWithModuleTag<LmnetModuleTag>(lmcore::LogLevel::kWarn, __FILE__, __LINE__, __FUNCTION__, fmt,    \
                                                    ##__VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGE(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = GetLmnetLoggerWithAutoInit();                                                                   \
        if (logger.ShouldLog(lmcore::LogLevel::kError)) {                                                              \
            logger.LogWithModuleTag<LmnetModuleTag>(lmcore::LogLevel::kError, __FILE__, __LINE__, __FUNCTION__, fmt,   \
                                                    ##__VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

#define LMNET_LOGF(fmt, ...)                                                                                           \
    do {                                                                                                               \
        auto &logger = GetLmnetLoggerWithAutoInit();                                                                   \
        if (logger.ShouldLog(lmcore::LogLevel::kFatal)) {                                                              \
            logger.LogWithModuleTag<LmnetModuleTag>(lmcore::LogLevel::kFatal, __FILE__, __LINE__, __FUNCTION__, fmt,   \
                                                    ##__VA_ARGS__);                                                    \
        }                                                                                                              \
    } while (0)

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_INTERNAL_LOGGER_H
