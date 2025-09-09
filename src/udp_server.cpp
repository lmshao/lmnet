/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "network/udp_server.h"

#include "network/network_logger.h"

#ifdef __linux__
#include "platforms/linux/port_utils.h"
#include "platforms/linux/udp_server_impl.h"
#elif _WIN32
#include "platforms/windows/port_utils.h"
#include "platforms/windows/udp_server_impl.h"
#endif

namespace lmshao::network {

UdpServer::UdpServer(std::string listenIp, uint16_t listenPort)
{
    impl_ = UdpServerImpl::Create(std::move(listenIp), listenPort);
    if (!impl_) {
        NETWORK_LOGE("Failed to create UDP server implementation");
    }
}

UdpServer::UdpServer(uint16_t listenPort) : UdpServer("0.0.0.0", listenPort) {}

UdpServer::~UdpServer() = default;

bool UdpServer::Init()
{
    if (!impl_) {
        NETWORK_LOGE("UDP server implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

bool UdpServer::Start()
{
    if (!impl_) {
        NETWORK_LOGE("UDP server implementation is not initialized");
        return false;
    }
    return impl_->Start();
}

bool UdpServer::Stop()
{
    if (!impl_) {
        NETWORK_LOGE("UDP server implementation is not initialized");
        return false;
    }
    return impl_->Stop();
}

void UdpServer::SetListener(std::shared_ptr<IServerListener> listener)
{
    if (!impl_) {
        NETWORK_LOGE("UDP server implementation is not initialized");
        return;
    }
    impl_->SetListener(listener);
}

socket_t UdpServer::GetSocketFd() const
{
    if (!impl_) {
        NETWORK_LOGE("UDP server implementation is not initialized");
        return INVALID_SOCKET;
    }
    return impl_->GetSocketFd();
}

uint16_t UdpServer::GetIdlePort()
{
    return PortUtils::GetIdleUdpPort();
}

uint16_t UdpServer::GetIdlePortPair()
{
    return PortUtils::GetIdleUdpPortPair();
}

} // namespace lmshao::network
