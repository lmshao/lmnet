/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LMNET_LOGGER_H
#define LMSHAO_LMNET_LMNET_LOGGER_H

#include <lmcore/logger.h>

namespace lmshao::lmnet {

// Module tag for Lmnet
struct LmnetModuleTag {};

/**
 * @brief Initialize Lmnet logger with specified settings
 */
inline void InitLmnetLogger(lmcore::LogLevel level =
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
                                lmcore::LogLevel::kDebug,
#else
                                lmcore::LogLevel::kWarn,
#endif
                            lmcore::LogOutput output = lmcore::LogOutput::CONSOLE, const std::string &filename = "")
{
    lmcore::LoggerRegistry::RegisterModule<LmnetModuleTag>("LMNet");
    lmcore::LoggerRegistry::InitLogger<LmnetModuleTag>(level, output, filename);
}

/**
 * @brief Set Lmnet logger level (use this to change level at runtime)
 */
inline void SetLmnetLogLevel(lmcore::LogLevel level)
{
    lmcore::LoggerRegistry::RegisterModule<LmnetModuleTag>("LMNet");
    auto &logger = lmcore::LoggerRegistry::GetLogger<LmnetModuleTag>();
    logger.SetLevel(level);
}

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LOGGER_H