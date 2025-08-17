/**
 * @file unix_client.h
 * @brief Unix Domain Socket Client Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_UNIX_CLIENT_H
#define NETWORK_UNIX_CLIENT_H

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include <memory>
#include <string>

#include "common.h"
#include "core-utils/data_buffer.h"
#include "iclient_listener.h"

namespace lmshao::network {

class IUnixClient;

class UnixClient : public Creatable<UnixClient> {
public:
    explicit UnixClient(const std::string &socketPath);
    ~UnixClient();

    bool Init();
    void SetListener(std::shared_ptr<IClientListener> listener);
    bool Connect();
    bool Send(const std::string &str);
    bool Send(const void *data, size_t len);
    bool Send(std::shared_ptr<DataBuffer> data);
    void Close();
    socket_t GetSocketFd() const;

private:
    std::shared_ptr<IUnixClient> impl_;
};

} // namespace lmshao::network

#endif // NETWORK_UNIX_CLIENT_H
