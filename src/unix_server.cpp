/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "lmnet/unix_server.h"

#include "base_server.h"
#include "internal_logger.h"

#if defined(__linux__)
#if defined(LMNET_LINUX_BACKEND_IOURING)
#include "platforms/linux/io_uring/unix_server_impl.h"
#else
#include "platforms/linux/epoll/unix_server_impl.h"
#endif
#elif defined(__APPLE__)
#include "platforms/darwin/unix_server_impl.h"
#endif

namespace lmshao::lmnet {

UnixServer::UnixServer(const std::string &socketPath)
{
    impl_ = UnixServerImpl::Create(socketPath);
    if (!impl_) {
        LMNET_LOGE("Failed to create Unix server implementation");
    }
}

UnixServer::~UnixServer() = default;

bool UnixServer::Init()
{
    if (!impl_) {
        LMNET_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

void UnixServer::SetListener(std::shared_ptr<IServerListener> listener)
{
    if (!impl_) {
        LMNET_LOGE("Unix server implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool UnixServer::Start()
{
    if (!impl_) {
        LMNET_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Start();
}

bool UnixServer::Stop()
{
    if (!impl_) {
        LMNET_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Stop();
}

socket_t UnixServer::GetSocketFd() const
{
    if (!impl_) {
        LMNET_LOGE("Unix server implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::lmnet
