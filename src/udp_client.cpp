//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "udp_client.h"
#include <arpa/inet.h>
#include <assert.h>
#include <cstddef>
#include <cstring>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include "event_reactor.h"
#include "log.h"

#define RECV_BUFFER_MAX_SIZE 4096

UdpClient::UdpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
}

UdpClient::~UdpClient()
{
    if (callbackThreads_) {
        callbackThreads_.reset();
    }
    Close();
}

bool UdpClient::Init()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket error: %s", strerror(errno));
        return false;
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    inet_aton(remoteIp_.c_str(), &serverAddr_.sin_addr);

    if (!localIp_.empty() || localPort_ != 0) {
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_aton(localIp_.c_str(), &localAddr.sin_addr);

        int ret = bind(socket_, (struct sockaddr *)&localAddr, (socklen_t)sizeof(localAddr));
        if (ret != 0) {
            NETWORK_LOGE("bind error: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

void UdpClient::Close()
{

    if (socket_ != INVALID_SOCKET) {
        EventReactor::GetInstance()->RemoveDescriptor(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool UdpClient::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    if (!listener_.expired() && callbackThreads_ == nullptr) {
        callbackThreads_ = std::make_unique<ThreadPool>(1, 1, "UdpClient-cb");
        EventReactor::GetInstance()->AddDescriptor(socket_, [&](int fd) { this->HandleReceive(fd); });
    }

    size_t nbytes = sendto(socket_, data, len, 0, (struct sockaddr *)&serverAddr_, (socklen_t)(sizeof(serverAddr_)));
    if (nbytes == -1) {
        NETWORK_LOGE("sendto error: %s", strerror(errno));
        return false;
    }

    return true;
}

bool UdpClient::Send(const std::string &str)
{
    return UdpClient::Send(str.c_str(), str.length());
}

bool UdpClient::Send(std::shared_ptr<DataBuffer> data)
{
    return UdpClient::Send((char *)data->Data(), data->Size());
}

void UdpClient::HandleReceive(int fd)
{
    NETWORK_LOGD("fd: %d", fd);
    static char buffer[RECV_BUFFER_MAX_SIZE] = {};
    memset(buffer, 0, RECV_BUFFER_MAX_SIZE);

    assert(socket_ == fd);
    ssize_t nbytes = recv(fd, buffer, sizeof(buffer), 0);
    if (nbytes > 0) {
        if (!listener_.expired()) {
            auto dataBuffer = std::make_shared<DataBuffer>(nbytes);
            dataBuffer->Assign(buffer, nbytes);
            callbackThreads_->AddTask([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnReceive(dataBuffer);
                }
            });
        }
    } else if (nbytes < 0) {
        std::string info = strerror(errno);
        NETWORK_LOGE("recv error: %s", info.c_str());
        EventReactor::GetInstance()->RemoveDescriptor(fd);
        close(fd);

        if (!listener_.expired()) {
            callbackThreads_->AddTask([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnError(info);
                    Close();
                } else {
                    NETWORK_LOGE("not found listener!");
                }
            });
        }
    } else if (nbytes == 0) {
        NETWORK_LOGW("Disconnect fd[%d]", fd);
        EventReactor::GetInstance()->RemoveDescriptor(fd);
        close(fd);

        if (!listener_.expired()) {
            callbackThreads_->AddTask([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnClose();
                    Close();
                } else {
                    NETWORK_LOGE("not found listener!");
                }
            });
        }
    }
}