//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "udp_client.h"

#include <arpa/inet.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include "event_reactor.h"
#include "log.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;
UdpClient::UdpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("UdpClientCb");
}

UdpClient::~UdpClient()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
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

    taskQueue_->Start();
    EventReactor::GetInstance()->AddDescriptor(socket_, [&](int fd) { this->HandleReceive(fd); });

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
    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_unique<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    readBuffer_->Clear();

    assert(socket_ == fd);
    ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), 0);
    if (nbytes > 0) {
        if (!listener_.expired()) {
            auto dataBuffer = std::make_shared<DataBuffer>(nbytes);
            dataBuffer->Assign(readBuffer_->Data(), nbytes);

            auto task = std::make_shared<TaskHandler<void>>([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnReceive(fd, dataBuffer);
                }
            });

            taskQueue_->EnqueueTask(task);
        }
    } else if (nbytes < 0) {
        std::string info = strerror(errno);
        NETWORK_LOGE("recv error: %s", info.c_str());
        EventReactor::GetInstance()->RemoveDescriptor(fd);
        close(fd);

        if (!listener_.expired()) {
            auto task = std::make_shared<TaskHandler<void>>([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnError(fd, info);
                    Close();
                } else {
                    NETWORK_LOGE("not found listener!");
                }
            });

            taskQueue_->EnqueueTask(task);
        }
    } else if (nbytes == 0) {
        NETWORK_LOGW("Disconnect fd[%d]", fd);
        EventReactor::GetInstance()->RemoveDescriptor(fd);
        close(fd);

        if (!listener_.expired()) {
            auto task = std::make_shared<TaskHandler<void>>([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    listener->OnClose(fd);
                    Close();
                } else {
                    NETWORK_LOGE("not found listener!");
                }
            });

            taskQueue_->EnqueueTask(task);
        }
    }
}