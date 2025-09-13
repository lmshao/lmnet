/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
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
 * @param level Log level (default: Debug in debug builds, Warn in release builds)
 * @param output Output destination (default: CONSOLE)
 * @param filename Log file name (optional)
 */
inline void InitLmnetLogger(lmshao::lmcore::LogLevel level =
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
                                lmshao::lmcore::LogLevel::kDebug,
#else
                                lmshao::lmcore::LogLevel::kWarn,
#endif
                            lmshao::lmcore::LogOutput output = lmshao::lmcore::LogOutput::CONSOLE,
                            const std::string &filename = "")
{
    // Register module if not already registered
    lmshao::lmcore::LoggerRegistry::RegisterModule<LmnetModuleTag>("Lmnet");
    lmshao::lmcore::LoggerRegistry::InitLogger<LmnetModuleTag>(level, output, filename);
}

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LOGGER_H