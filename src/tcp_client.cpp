//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "tcp_client.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "event_reactor.h"
#include "log.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;
TcpClient::TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpClientCb");
}

TcpClient::~TcpClient()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool TcpClient::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("Socket error: %s", strerror(errno));
        return false;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        struct sockaddr_in localAddr;
        memset(&localAddr, 0, sizeof(localAddr));
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_aton(localIp_.c_str(), &localAddr.sin_addr);

        int optval = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            NETWORK_LOGE("setsockopt SO_REUSEADDR error: %s", strerror(errno));
            return false;
        }

        int ret = bind(socket_, (struct sockaddr *)&localAddr, (socklen_t)sizeof(localAddr));
        if (ret != 0) {
            NETWORK_LOGE("bind error: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

void TcpClient::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
    Init();
}

bool TcpClient::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    if (remoteIp_.empty()) {
        remoteIp_ = "127.0.0.1";
    }

    inet_aton(remoteIp_.c_str(), &serverAddr_.sin_addr);

    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        NETWORK_LOGE("connect(%s:%d) failed: %s", remoteIp_.c_str(), remotePort_, strerror(errno));
        ReInit();
        return false;
    }

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(socket_, &writefds);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    ret = select(socket_ + 1, NULL, &writefds, NULL, &timeout);
    if (ret > 0) {
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
            NETWORK_LOGE("getsockopt error, %s", strerror(errno));
            ReInit();
            return false;
        }

        if (error != 0) {
            NETWORK_LOGE("connect error, %s", strerror(errno));
            ReInit();
            return false;
        }
    } else {
        NETWORK_LOGE("connect error, %s", strerror(errno));
        ReInit();
        return false;
    }

    taskQueue_->Start();
    EventReactor::GetInstance()->AddDescriptor(socket_, [&](int fd) { this->HandleReceive(fd); });

    NETWORK_LOGD("Connect (%s:%d) success.", remoteIp_.c_str(), remotePort_);
    return true;
}

bool TcpClient::Send(const std::string &str)
{
    return TcpClient::Send(str.c_str(), str.length());
}

bool TcpClient::Send(const char *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    size_t bytes = ::send(socket_, data, len, 0);
    if (bytes != len) {
        NETWORK_LOGE("send failed: %s", strerror(errno));
        return false;
    }

    return true;
}

bool TcpClient::Send(std::shared_ptr<DataBuffer> data)
{
    return TcpClient::Send((char *)data->Data(), data->Size());
}

void TcpClient::Close()
{
    if (socket_ != INVALID_SOCKET) {
        EventReactor::GetInstance()->RemoveDescriptor(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void TcpClient::HandleReceive(int fd)
{
    // NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_unique<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    readBuffer_->Clear();

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
        };
        return;
    }

    if (errno == EAGAIN) {
        NETWORK_LOGD("recv EAGAIN");
        return;
    }

    if (nbytes < 0) {
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