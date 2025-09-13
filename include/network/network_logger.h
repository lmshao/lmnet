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

namespace lmshao::network {

// Module tag for Network
struct NetworkModuleTag {};

/**
 * @brief Initialize Network logger with specified settings
 * @param level Log level (default: Debug in debug builds, Warn in release builds)
 * @param output Output destination (default: CONSOLE)
 * @param filename Log file name (optional)
 */
inline void InitNetworkLogger(lmshao::coreutils::LogLevel level =
#if defined(_DEBUG) || defined(DEBUG) || !defined(NDEBUG)
                                  lmshao::coreutils::LogLevel::kDebug,
#else
                                  lmshao::coreutils::LogLevel::kWarn,
#endif
                              lmshao::coreutils::LogOutput output = lmshao::coreutils::LogOutput::CONSOLE,
                              const std::string &filename = "")
{
    // Register module if not already registered
    lmshao::coreutils::LoggerRegistry::RegisterModule<NetworkModuleTag>("Network");
    lmshao::coreutils::LoggerRegistry::InitLogger<NetworkModuleTag>(level, output, filename);
}

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_LOGGER_H