/**
 * UDP Client Windows Implementation Header
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_NETWORK_WINDOWS_UDP_CLIENT_IMPL_H
#define LMSHAO_NETWORK_WINDOWS_UDP_CLIENT_IMPL_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "coreutils/data_buffer.h"
#include "iocp_manager.h"
#include "iudp_client.h"
#include "network/common.h"
#include "network/iclient_listener.h"

namespace lmshao::network {

class UdpClientImpl final : public IUdpClient,
                            public std::enable_shared_from_this<UdpClientImpl>,
                            public Creatable<UdpClientImpl>,
                            public IIocpHandler {
    friend class Creatable<UdpClientImpl>;

public:
    ~UdpClientImpl();

    // IUdpClient interface
    bool Init() override; // create socket, optional bind, prepare remote address, attach IOCP
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }
    bool EnableBroadcast() override;                  // Enable UDP broadcast functionality
    bool Send(const std::string &str) override;       // convenience wrapper
    bool Send(const void *data, size_t len) override; // synchronous sendto (later async)
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override; // close socket & stop worker
    socket_t GetSocketFd() const override { return socket_; }

    UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    // Internal callback helpers used by IOCP worker (not part of public API)
    void HandleReceive(socket_t fd);
    void HandleConnectionClose(socket_t fd, bool error, const std::string &info);

    // IIocpHandler interface
    void OnIoCompletion(ULONG_PTR key, LPOVERLAPPED ov, DWORD bytes, bool success, DWORD error) override;

private:
    UdpClientImpl() = default;

    // --- internal state ---
    std::string remoteIp_;
    uint16_t remotePort_{0};
    std::string localIp_;
    uint16_t localPort_{0};
    socket_t socket_{INVALID_SOCKET};
    std::shared_ptr<IClientListener> listener_;

    // IOCP specific state
    struct sockaddr_in remoteAddr_ {}; // peer
    bool running_{false};              // client state flag
    // PerIoContext defined in iocp_utils.h (alias to win::UdpPerIoContext). Forward declaration not needed.
    void PostRecv(); // post one WSARecvFrom overlapped
};

} // namespace lmshao::network

#endif // LMSHAO_NETWORK_WINDOWS_UDP_CLIENT_IMPL_H
