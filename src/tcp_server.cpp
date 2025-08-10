/**
 * @file tcp_server.cpp
 * @brief TCP Server Public Interface Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_server.h"

#include "log.h"

#ifdef __linux__
#include "platforms/linux/tcp_server_impl.h"
#elif _WIN32
#include "platforms/windows/tcp_server_impl.h"
#endif

namespace lmshao::network {
TcpServer::TcpServer(std::string listenIp, uint16_t listenPort)
{
    impl_ = TcpServerImpl::Create(std::move(listenIp), listenPort);
    if (!impl_) {
        NETWORK_LOGE("Failed to create TCP server implementation");
    }
}

TcpServer::TcpServer(uint16_t listenPort)
{
    impl_ = TcpServerImpl::Create(listenPort);
    if (!impl_) {
        NETWORK_LOGE("Failed to create TCP server implementation");
    }
}

TcpServer::~TcpServer() = default;

bool TcpServer::Init()
{
    if (!impl_) {
        NETWORK_LOGE("TCP server implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

void TcpServer::SetListener(std::shared_ptr<IServerListener> listener)
{
    if (!impl_) {
        NETWORK_LOGE("TCP server implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool TcpServer::Start()
{
    if (!impl_) {
        NETWORK_LOGE("TCP server implementation is not initialized");
        return false;
    }
    return impl_->Start();
}

bool TcpServer::Stop()
{
    if (!impl_) {
        NETWORK_LOGE("TCP server implementation is not initialized");
        return false;
    }
    return impl_->Stop();
}

socket_t TcpServer::GetSocketFd() const
{
    if (!impl_) {
        NETWORK_LOGE("TCP server implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}
} // namespace lmshao::network
