/**
 * @file udp_server_impl.cpp
 * @brief UDP Server Linux Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_server_impl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>

#include "event_reactor.h"
#include "log.h"
#include "session.h"

namespace lmshao::network {

// UDP Session implementation
class UdpSession : public Session {
public:
    UdpSession(socket_t fd, const std::string &remoteIp, uint16_t remotePort, UdpServerImpl *server)
        : fd_(fd), server_(server)
    {
        host = remoteIp;
        port = remotePort;
        fd = fd_;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        if (!server_)
            return false;
        return server_->Send(fd_, host, port, buffer->Data(), buffer->Size());
    }

    bool Send(const std::string &str) const override
    {
        if (!server_)
            return false;
        return server_->Send(fd_, host, port, str);
    }

    bool Send(const void *data, size_t size) const override
    {
        if (!server_)
            return false;
        return server_->Send(fd_, host, port, data, size);
    }

    std::string ClientInfo() const override { return host + ":" + std::to_string(port); }

private:
    socket_t fd_;
    UdpServerImpl *server_;
};

// UDP Server Handler
class UdpServerHandler : public EventHandler {
public:
    UdpServerHandler(socket_t fd, std::weak_ptr<UdpServerImpl> server) : fd_(fd), server_(server) {}

    void HandleRead(int fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(int fd) override {}

    void HandleError(int fd) override
    {
        NETWORK_LOGE("UDP server connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            NETWORK_LOGE("UDP server socket error occurred");
        }
    }

    void HandleClose(int fd) override
    {
        NETWORK_LOGD("UDP server connection close on fd: %d", fd);
        if (auto server = server_.lock()) {
            NETWORK_LOGD("UDP server socket closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    socket_t fd_;
    std::weak_ptr<UdpServerImpl> server_;
};

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port)
    : ip_(std::move(ip)), port_(port), taskQueue_(std::make_unique<TaskQueue>("UdpServer")),
      readBuffer_(std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE))
{
}

UdpServerImpl::~UdpServerImpl()
{
    Stop();
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool UdpServerImpl::Init()
{
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        NETWORK_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    // Set socket to non-blocking
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        NETWORK_LOGE("fcntl F_GETFL failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (fcntl(socket_, F_SETFL, flags | O_NONBLOCK) == -1) {
        NETWORK_LOGE("fcntl F_SETFL failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    // Set SO_REUSEADDR
    int reuse = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        NETWORK_LOGE("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    std::memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port_);

    if (inet_pton(AF_INET, ip_.c_str(), &serverAddr_.sin_addr) <= 0) {
        NETWORK_LOGE("inet_pton failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (bind(socket_, reinterpret_cast<struct sockaddr *>(&serverAddr_), sizeof(serverAddr_)) < 0) {
        NETWORK_LOGE("bind failed: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    NETWORK_LOGD("UDP server initialized on %s:%d", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("Socket is not initialized");
        return false;
    }

    // Create event handler
    auto self = shared_from_this();
    serverHandler_ = std::make_shared<UdpServerHandler>(socket_, self);

    // Add to event loop
    if (!EventReactor::GetInstance()->RegisterHandler(serverHandler_)) {
        NETWORK_LOGE("Failed to add server handler to event reactor");
        return false;
    }

    // Start task queue
    if (taskQueue_->Start() != 0) {
        NETWORK_LOGE("Failed to start task queue");
        EventReactor::GetInstance()->RemoveHandler(socket_);
        return false;
    }

    NETWORK_LOGD("UDP server started successfully on %s:%d", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Stop()
{
    NETWORK_LOGD("Stopping UDP server");

    // Stop task queue first
    if (taskQueue_) {
        taskQueue_->Stop();
    }

    // Remove from event loop
    if (serverHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        serverHandler_.reset();
    }

    // Close socket
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }

    NETWORK_LOGD("UDP server stopped");
    return true;
}

void UdpServerImpl::HandleReceive(socket_t fd)
{
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);

    readBuffer_->Clear();
    readBuffer_->SetCapacity(RECV_BUFFER_MAX_SIZE);

    ssize_t bytesRead = recvfrom(fd, readBuffer_->Data(), readBuffer_->Capacity(), 0,
                                 reinterpret_cast<struct sockaddr *>(&clientAddr), &clientAddrLen);

    if (bytesRead > 0) {
        readBuffer_->SetSize(bytesRead);

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        uint16_t clientPort = ntohs(clientAddr.sin_port);

        // Create session info
        auto session = std::make_shared<UdpSession>(fd, clientIP, clientPort, this);

        if (auto listener = listener_.lock()) {
            // Create a copy of the data
            auto dataBuffer = std::make_shared<DataBuffer>(*readBuffer_);

            // For UDP, call the listener directly
            listener->OnReceive(session, dataBuffer);
        }
    } else if (bytesRead == 0) {
        NETWORK_LOGD("UDP peer closed connection");
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            NETWORK_LOGE("recvfrom failed: %s", strerror(errno));
        }
    }
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("Socket is not initialized");
        return false;
    }

    struct sockaddr_in clientAddr;
    std::memset(&clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip.c_str(), &clientAddr.sin_addr) <= 0) {
        NETWORK_LOGE("inet_pton failed for IP: %s", ip.c_str());
        return false;
    }

    ssize_t bytesSent =
        sendto(socket_, data, len, 0, reinterpret_cast<struct sockaddr *>(&clientAddr), sizeof(clientAddr));

    if (bytesSent < 0) {
        NETWORK_LOGE("sendto failed: %s", strerror(errno));
        return false;
    }

    if (static_cast<size_t>(bytesSent) != len) {
        NETWORK_LOGW("Partial send: sent %zd bytes out of %zu", bytesSent, len);
        return false;
    }

    return true;
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, const std::string &str)
{
    return Send(fd, std::move(ip), port, str.data(), str.size());
}

bool UdpServerImpl::Send(int fd, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    if (!data) {
        NETWORK_LOGE("DataBuffer is null");
        return false;
    }
    return Send(fd, std::move(ip), port, data->Data(), data->Size());
}

} // namespace lmshao::network
