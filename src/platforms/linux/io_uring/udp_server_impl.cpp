/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_server_impl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "session_impl.h"

namespace lmshao::lmnet {

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port) : ip_(std::move(ip)), port_(port) {}

UdpServerImpl::~UdpServerImpl()
{
    Stop();
}

bool UdpServerImpl::Init()
{
    socket_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socket_ < 0) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LMNET_LOGE("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    std::memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port_);

    if (inet_pton(AF_INET, ip_.c_str(), &serverAddr_.sin_addr) <= 0) {
        LMNET_LOGE("inet_pton failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (bind(socket_, reinterpret_cast<struct sockaddr *>(&serverAddr_), sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("bind failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    LMNET_LOGD("UDP server initialized on %s:%d", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket is not initialized");
        return false;
    }

    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        return false;
    }

    is_running_.store(true);
    StartReceive();

    LMNET_LOGD("UDP server started");
    return true;
}

bool UdpServerImpl::Stop()
{
    if (is_running_.exchange(false) && socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
        LMNET_LOGD("UDP server stopped");
    }
    return true;
}

void UdpServerImpl::StartReceive()
{
    if (!is_running_.load())
        return;

    auto buffer = std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    auto cb = [this](int, std::shared_ptr<DataBuffer> buf, int res, const sockaddr_in &from) {
        HandleReceive(buf, res, from);
    };
    IoUringManager::GetInstance().SubmitRecvFromRequest(socket_, buffer, cb);
}

void UdpServerImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            uint16_t port = ntohs(from_addr.sin_port);

            auto session = std::make_shared<SessionImpl>(socket_, std::string(ip_str), port, shared_from_this());
            listener->OnReceive(session, buffer);
        }
        StartReceive(); // Continue receiving
    } else if (bytes_read < 0) {
        if (is_running_.load()) {
            LMNET_LOGE("UDP receive error: %s", strerror(-bytes_read));
        }
    }
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET || !data || len == 0) {
        return false;
    }

    auto buffer = std::make_shared<DataBuffer>(len);
    buffer->Append(static_cast<const char *>(data), len);

    return Send(ip, port, buffer);
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, const std::string &str)
{
    return Send(ip, port, str.data(), str.size());
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    if (!data)
        return false;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dest_addr.sin_addr) <= 0) {
        return false;
    }

    IoUringManager::GetInstance().SubmitSendToRequest(socket_, data, dest_addr, [this](int, int res) {
        if (res < 0) {
            LMNET_LOGE("sendto failed: %s", strerror(-res));
        }
    });

    return true;
}

bool UdpServerImpl::Send(socket_t fd, std::string ip, uint16_t port, const void *data, size_t len)
{
    return Send(ip, port, data, len);
}

bool UdpServerImpl::Send(socket_t fd, std::string ip, uint16_t port, const std::string &str)
{
    return Send(ip, port, str);
}

bool UdpServerImpl::Send(socket_t fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    return Send(ip, port, data);
}

} // namespace lmshao::lmnet
