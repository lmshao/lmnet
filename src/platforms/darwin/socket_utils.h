/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_DARWIN_SOCKET_UTILS_H
#define LMSHAO_LMNET_DARWIN_SOCKET_UTILS_H

#include "lmnet/common.h"

namespace lmshao::lmnet::darwin {

bool SetNonBlocking(socket_t fd);
bool SetCloseOnExec(socket_t fd);
void DisableSigpipe(socket_t fd);
bool ConfigureAcceptedSocket(socket_t fd);
socket_t CreateStreamSocket(int domain);

} // namespace lmshao::lmnet::darwin

#endif // LMSHAO_LMNET_DARWIN_SOCKET_UTILS_H
