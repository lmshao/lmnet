/**
 * @file port_utils.cpp
 * @brief Port Discovery Utilities Implementation for Windows Platform
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "port_utils.h"

namespace lmshao::network {

uint16_t PortUtils::nextPort_ = PortUtils::UDP_PORT_START;

// Empty implementations for Windows platform
// These can be implemented later with Windows-specific socket APIs

} // namespace lmshao::network
