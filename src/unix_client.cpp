/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "lmnet/unix_client.h"

#include "internal_logger.h"
#include "iunix_client.h"

#if defined(__linux__)
#if defined(LMNET_LINUX_BACKEND_IOURING)
#include "platforms/linux/io_uring/unix_client_impl.h"
#else
#include "platforms/linux/epoll/unix_client_impl.h"
#endif
#elif defined(__APPLE__)
#include "platforms/darwin/unix_client_impl.h"
#endif

namespace lmshao::lmnet {

UnixClient::UnixClient(const std::string &socketPath)
{
    impl_ = std::make_shared<UnixClientImpl>(socketPath);
    if (!impl_) {
        LMNET_LOGE("Failed to create Unix client implementation");
    }
}

UnixClient::~UnixClient() {}

bool UnixClient::Init()
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

bool UnixClient::Connect()
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Connect();
}

void UnixClient::SetListener(std::shared_ptr<IClientListener> listener)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool UnixClient::Send(const std::string &str)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(str);
}

bool UnixClient::Send(const void *data, size_t len)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(data, len);
}

bool UnixClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->Send(std::move(data));
}

bool UnixClient::SendFds(const std::vector<int> &fds)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->SendFds(fds);
}

bool UnixClient::SendWithFds(std::shared_ptr<DataBuffer> data, const std::vector<int> &fds)
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return false;
    }
    return impl_->SendWithFds(data, fds);
}

void UnixClient::Close()
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return;
    }
    impl_->Close();
}

socket_t UnixClient::GetSocketFd() const
{
    if (!impl_) {
        LMNET_LOGE("Unix client implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::lmnet
