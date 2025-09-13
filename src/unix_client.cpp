/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include "network/unix_client.h"

#include "internal_logger.h"
#include "iunix_client.h"

#ifdef __linux__
#include "platforms/linux/unix_client_impl.h"
#endif

namespace lmshao::network {

UnixClient::UnixClient(const std::string &socketPath)
{
    impl_ = UnixClientImpl::Create(socketPath);
    if (!impl_) {
        NETWORK_LOGE("Failed to create Unix client implementation");
    }
}

UnixClient::~UnixClient() {}

bool UnixClient::Init()
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

bool UnixClient::Connect()
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Connect();
}

void UnixClient::SetListener(std::shared_ptr<IClientListener> listener)
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool UnixClient::Send(const std::string &str)
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(str);
}

bool UnixClient::Send(const void *data, size_t len)
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(data, len);
}

bool UnixClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(std::move(data));
}

void UnixClient::Close()
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return;
    }
    impl_->Close();
}

socket_t UnixClient::GetSocketFd() const
{
    if (!impl_) {
        NETWORK_LOGE("Unix client implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::network
