/**
 *
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_UDP_CLIENT_IMPL_H
#define LMSHAO_LMNET_UDP_CLIENT_IMPL_H

#include <lmcore/task_queue.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "iudp_client.h"
#include "lmnet/iclient_listener.h"
#include "packet_orderer.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskQueue;

class UdpClientImpl final : public IUdpClient, public std::enable_shared_from_this<UdpClientImpl> {

public:
    UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);
    ~UdpClientImpl() override;

    bool Init() override;
    void SetListener(std::shared_ptr<IClientListener> listener) override;
    bool EnableBroadcast() override;
    bool Send(const std::string &str) override;
    bool Send(const void *data, size_t len) override;
    bool Send(std::shared_ptr<DataBuffer> data) override;
    void Close() override;
    socket_t GetSocketFd() const override;

private:
    void StartReceiving();
    void SubmitReceive();
    void HandleSend(DWORD bytesOrError);
    void HandleClose(bool isError, const std::string &reason);
    void DeliverOrdered(std::shared_ptr<DataBuffer> buffer, const sockaddr_storage &fromAddr, int fromLen);

private:
    std::string remoteIp_;
    uint16_t remotePort_;
    std::string localIp_;
    uint16_t localPort_;

    SOCKET socket_{INVALID_SOCKET};
    sockaddr_storage remoteAddr_{};
    int remoteAddrLen_{0};
    sockaddr_storage localAddr_{};
    int localAddrLen_{0};
    bool use_ipv6_{false};

    std::atomic<bool> isRunning_{false};
    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;

    // Packet ordering for IOCP
    std::unique_ptr<PacketOrderer> packet_orderer_;
    std::atomic<uint64_t> receive_seq_counter_{0};

    // Number of concurrent receive operations (restored for performance)
    static constexpr int CONCURRENT_RECEIVES = 4;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_UDP_CLIENT_IMPL_H
