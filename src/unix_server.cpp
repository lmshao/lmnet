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

#include "network/unix_server.h"

#include "internal_logger.h"

#ifdef __linux__
#include "platforms/linux/unix_server_impl.h"
#endif

namespace lmshao::network {

UnixServer::UnixServer(const std::string &socketPath)
{
    impl_ = UnixServerImpl::Create(socketPath);
    if (!impl_) {
        NETWORK_LOGE("Failed to create Unix server implementation");
    }
}

UnixServer::~UnixServer() = default;

bool UnixServer::Init()
{
    if (!impl_) {
        NETWORK_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Init();
}

void UnixServer::SetListener(std::shared_ptr<IServerListener> listener)
{
    if (!impl_) {
        NETWORK_LOGE("Unix server implementation is not initialized");
        return;
    }
    impl_->SetListener(std::move(listener));
}

bool UnixServer::Start()
{
    if (!impl_) {
        NETWORK_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Start();
}

bool UnixServer::Stop()
{
    if (!impl_) {
        NETWORK_LOGE("Unix server implementation is not initialized");
        return false;
    }
    return impl_->Stop();
}

socket_t UnixServer::GetSocketFd() const
{
    if (!impl_) {
        NETWORK_LOGE("Unix server implementation is not initialized");
        return -1;
    }
    return impl_->GetSocketFd();
}

} // namespace lmshao::network
