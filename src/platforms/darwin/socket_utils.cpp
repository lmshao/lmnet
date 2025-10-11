/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "socket_utils.h"

#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "internal_logger.h"

namespace lmshao::lmnet::darwin {

bool SetNonBlocking(socket_t fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        LMNET_LOGE("fcntl(F_GETFL) failed: %s", strerror(errno));
        return false;
    }
    if ((flags & O_NONBLOCK) != 0) {
        return true;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        LMNET_LOGE("fcntl(F_SETFL) failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool SetCloseOnExec(socket_t fd)
{
    int current = fcntl(fd, F_GETFD, 0);
    if (current == -1) {
        LMNET_LOGE("fcntl(F_GETFD) failed: %s", strerror(errno));
        return false;
    }
    if (fcntl(fd, F_SETFD, current | FD_CLOEXEC) == -1) {
        LMNET_LOGE("fcntl(F_SETFD) failed: %s", strerror(errno));
        return false;
    }
    return true;
}

void DisableSigpipe(socket_t fd)
{
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == -1) {
        LMNET_LOGW("setsockopt SO_NOSIGPIPE failed on fd %d: %s", fd, strerror(errno));
    }
}

bool ConfigureAcceptedSocket(socket_t fd)
{
    if (!SetCloseOnExec(fd)) {
        return false;
    }
    if (!SetNonBlocking(fd)) {
        return false;
    }
    DisableSigpipe(fd);
    return true;
}

socket_t CreateStreamSocket(int domain)
{
    socket_t fd = socket(domain, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) {
        LMNET_LOGE("socket() failed: %s", strerror(errno));
        return INVALID_SOCKET;
    }

    if (!ConfigureAcceptedSocket(fd)) {
        close(fd);
        return INVALID_SOCKET;
    }

    return fd;
}

} // namespace lmshao::lmnet::darwin
