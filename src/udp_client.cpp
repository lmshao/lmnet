/**
 * @file udp_client.cpp
 * @brief UDP Client Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client.h"

#include "network_log.h"

#ifdef __linux__
#include "platforms/linux/udp_client_impl.h"
#elif _WIN32
#include "platforms/windows/udp_client_impl.h"
#endif

namespace lmshao::network {

UdpClient::UdpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
{
    impl_ = UdpClientImpl::Create(std::move(remoteIp), remotePort, std::move(localIp), localPort);
    if (!impl_) {
        NETWORK_LOGE("Failed to create UDP client implementation");
    }
}

UdpClient::~UdpClient() {}

bool UdpClient::Init()
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return false;
    }

    return impl_->Init();
}

void UdpClient::SetListener(std::shared_ptr<IClientListener> listener)
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool UdpClient::Send(const std::string &str)
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return false;
    }
    return impl_->Send(str);
}

bool UdpClient::Send(const void *data, size_t len)
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return false;
    }
    return impl_->Send(data, len);
}

bool UdpClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return false;
    }
    return impl_->Send(std::move(data));
}

void UdpClient::Close()
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return;
    }
    impl_->Close();
}

socket_t UdpClient::GetSocketFd() const
{
    if (!impl_) {
        NETWORK_LOGE("UDP client implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::network