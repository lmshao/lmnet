//
// Copyright © 2024-2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "udp_client.h"

#include <arpa/inet.h>
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

#include "event_reactor.h"
#include "log.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class UdpClientHandler : public EventHandler {
public:
    UdpClientHandler(int fd, std::weak_ptr<UdpClient> client) : fd_(fd), client_(client) {}

    void HandleRead(int fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(int fd) override {}

    void HandleError(int fd) override
    {
        NETWORK_LOGE("UDP client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(int fd) override
    {
        NETWORK_LOGD("UDP client connection close on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    int fd_;
    std::weak_ptr<UdpClient> client_;
};

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
    socket_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
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

    clientHandler_ = std::make_shared<UdpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(clientHandler_)) {
        NETWORK_LOGE("Failed to register UDP client handler");
        return false;
    }

    NETWORK_LOGD("UdpClient initialized with new EventHandler interface");
    return true;
}

void UdpClient::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

bool UdpClient::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    if (!data || len == 0) {
        NETWORK_LOGE("invalid send parameters: data=%p, len=%zu", data, len);
        return false;
    }

    ssize_t nbytes = sendto(socket_, data, len, 0, (struct sockaddr *)&serverAddr_, (socklen_t)(sizeof(serverAddr_)));
    if (nbytes == -1) {
        NETWORK_LOGE("sendto error: %s", strerror(errno));
        return false;
    }

    return true;
}

bool UdpClient::Send(const std::string &str)
{
    if (str.empty()) {
        NETWORK_LOGE("invalid send parameters: empty string");
        return false;
    }
    return Send(str.data(), str.size());
}

bool UdpClient::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data)
        return false;
    return Send(data->Data(), data->Size());
}

void UdpClient::HandleReceive(int fd)
{
    NETWORK_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);
                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>([listenerWeak, dataBuffer, fd]() {
                    auto listener = listenerWeak.lock();
                    if (listener) {
                        listener->OnReceive(fd, dataBuffer);
                    }
                });
                if (taskQueue_) {
                    taskQueue_->EnqueueTask(task);
                }
            }
            continue;
        } else if (nbytes == 0) {
            NETWORK_LOGW("Disconnect fd[%d]", fd);
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // Usually same value, but check both for portability
                // Normal case: no data available to read, return directly
                break;
            }

            std::string info = strerror(errno);
            NETWORK_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void UdpClient::HandleConnectionClose(int fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing UDP client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
                 isError ? "true" : "false");

    if (socket_ != fd) {
        NETWORK_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance()->RemoveHandler(fd);
    close(fd);
    socket_ = INVALID_SOCKET;
    clientHandler_.reset();

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, reason, isError, fd]() {
            auto listener = listenerWeak.lock();
            if (listener != nullptr) {
                if (isError) {
                    listener->OnError(fd, reason);
                } else {
                    listener->OnClose(fd);
                }
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    }
}