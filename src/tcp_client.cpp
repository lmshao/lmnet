/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "lmnet/tcp_client.h"

#include "internal_logger.h"

#ifdef __linux__
#include "platforms/linux/epoll/tcp_client_impl.h"
#elif _WIN32
#include "platforms/windows/tcp_client_impl.h"
#endif

namespace lmshao::lmnet {

TcpClient::TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
{
    impl_ = TcpClientImpl::Create(std::move(remoteIp), remotePort, std::move(localIp), localPort);
    if (!impl_) {
        LMNET_LOGE("Failed to create TCP client implementation");
    }
}

TcpClient::~TcpClient() {}

bool TcpClient::Init()
{
    if (!impl_) {
        LMNET_LOGE("TCP client implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

bool TcpClient::Connect()
{
    if (!impl_) {
        LMNET_LOGE("TCP client implementation is not initialized");
        return false;
    }
    return impl_->Connect();
}

void TcpClient::SetListener(std::shared_ptr<IClientListener> listener)
{
    if (!impl_) {
        LMNET_LOGE("TCP client implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool TcpClient::Send(const std::string &str)
{
    if (!impl_) {
        LMNET_LOGE("TCP client implementation is not initialized");
        return false;
    }
    return impl_->Send(str);
}

bool TcpClient::Send(const void *data, size_t len)
{
    if (!impl_) {
        LMNET_LOGE("TCP client implementation is not initialized");
        return false;
    }
    return impl_->Send(data, len);
}

bool TcpClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (!impl_) {
        LMNET_LOGE("UDP client implementation is not initialized");
        return false;
    }
    return impl_->Send(std::move(data));
}

void TcpClient::Close()
{
    if (!impl_) {
        LMNET_LOGE("UDP client implementation is not initialized");
        return;
    }
    impl_->Close();
}

socket_t TcpClient::GetSocketFd() const
{
    if (!impl_) {
        LMNET_LOGE("UDP client implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::lmnet