/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "internal_logger.h"
#include "io_uring_manager.h"

namespace lmshao::lmnet {

const int RECV_BUFFER_MAX_SIZE = 4096;

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
}

UdpClientImpl::~UdpClientImpl()
{
    Close();
}

bool UdpClientImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket error: %s", strerror(errno));
        return false;
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    inet_aton(remoteIp_.c_str(), &serverAddr_.sin_addr);

    if (!localIp_.empty() || localPort_ != 0) {
        memset(&localAddr_, 0, sizeof(localAddr_));
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_aton(localIp_.c_str(), &localAddr_.sin_addr);

        if (bind(socket_, (struct sockaddr *)&localAddr_, sizeof(localAddr_)) != 0) {
            LMNET_LOGE("bind error: %s", strerror(errno));
            close(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    }

    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        return false;
    }

    is_running_.store(true);
    StartReceive();
    LMNET_LOGD("UdpClientImpl initialized");
    return true;
}

bool UdpClientImpl::EnableBroadcast()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized, call Init() first");
        return false;
    }

    int broadcast = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LMNET_LOGE("Failed to enable broadcast: %s", strerror(errno));
        return false;
    }

    LMNET_LOGD("Broadcast enabled successfully");
    return true;
}

void UdpClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET && is_running_.exchange(false)) {
        HandleClose(false, "Manual close");
    }
}

void UdpClientImpl::HandleClose(bool is_error, const std::string &reason)
{
    if (auto listener = listener_.lock()) {
        listener->OnError(is_error ? -1 : 0, reason);
    }
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET || !data || len == 0) {
        return false;
    }

    auto buffer = std::make_shared<DataBuffer>(len);
    buffer->Append(static_cast<const char *>(data), len);

    auto cb = [this](int, int result) {
        if (result < 0) {
            LMNET_LOGE("UDP send failed: %s", strerror(-result));
        }
    };

    return IoUringManager::GetInstance().SubmitWriteRequest(socket_, buffer, cb);
}

bool UdpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data)
        return false;
    return Send(data->Data(), data->Size());
}

void UdpClientImpl::StartReceive()
{
    if (!is_running_.load())
        return;

    auto buffer = std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    auto cb = [this](int fd, std::shared_ptr<DataBuffer> buf, int res, const sockaddr_in &from) {
        HandleReceive(buf, res, from);
    };
    IoUringManager::GetInstance().SubmitRecvFromRequest(socket_, buffer, cb);
}

void UdpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            // TODO: Pass from_addr to listener if needed
            listener->OnReceive(socket_, buffer);
        }
        StartReceive(); // Continue receiving
    } else if (bytes_read < 0) {
        if (is_running_.load()) {
            LMNET_LOGE("UDP receive error: %s", strerror(-bytes_read));
        }
        HandleClose(true, "Receive error");
    }
}

} // namespace lmshao::lmnet