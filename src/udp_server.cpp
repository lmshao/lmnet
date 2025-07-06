//
// Copyright © 2024-2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "udp_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

#include "event_reactor.h"
#include "log.h"
#include "session_impl.h"

constexpr int RECV_BUFFER_MAX_SIZE = 4096;
constexpr uint16_t UDP_SERVER_DEFAULT_PORT_START = 10000;
static uint16_t gIdlePort = UDP_SERVER_DEFAULT_PORT_START;

class UdpServerHandler : public EventHandler {
public:
    explicit UdpServerHandler(std::weak_ptr<UdpServer> server) : server_(server) {}

    void HandleRead(int fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(int fd) override {}

    void HandleError(int fd) override { NETWORK_LOGE("UDP server socket error on fd: %d", fd); }

    void HandleClose(int fd) override { NETWORK_LOGD("UDP server socket close on fd: %d", fd); }

    int GetHandle() const override
    {
        if (auto server = server_.lock()) {
            return server->GetSocketFd();
        }
        return -1;
    }

    int GetEvents() const override
    {
        return static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) |
               static_cast<int>(EventType::CLOSE);
    }

private:
    std::weak_ptr<UdpServer> server_;
};

UdpServer::~UdpServer()
{
    NETWORK_LOGD("destructor");
    Stop();
}

bool UdpServer::Init()
{
    socket_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket error: %s", strerror(errno));
        return false;
    }
    NETWORK_LOGD("fd:%d", socket_);

    int optval = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        NETWORK_LOGE("setsockopt error: %s", strerror(errno));
        return false;
    }

    // int bufferSize = 819200;
    // if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &bufferSize, sizeof(bufferSize)) < 0) {
    //     NETWORK_LOGE("setsockopt error: %s", strerror(errno));
    //     return false;
    // }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(localPort_);
    inet_aton(localIp_.c_str(), &serverAddr_.sin_addr);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        NETWORK_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    taskQueue_ = std::make_unique<TaskQueue>("UdpServerCb");

    return true;
}

bool UdpServer::Start()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    taskQueue_->Start();

    serverHandler_ = std::make_shared<UdpServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(serverHandler_)) {
        NETWORK_LOGE("Failed to register UDP server handler");
        return false;
    }

    NETWORK_LOGD("UdpServer started with new EventHandler interface");
    return true;
}

bool UdpServer::Stop()
{
    if (socket_ != INVALID_SOCKET && serverHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
    }

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }

    NETWORK_LOGD("UdpServer stopped");
    return true;
}

bool UdpServer::Send(int fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    struct sockaddr_in remoteAddr;
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(port);
    inet_aton(host.c_str(), &remoteAddr.sin_addr);

    size_t nbytes =
        sendto(socket_, buffer->Data(), buffer->Size(), 0, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr));
    if (nbytes != buffer->Size()) {
        NETWORK_LOGE("send error: %s %zd/%zu", strerror(errno), nbytes, buffer->Size());
        return false;
    }

    return true;
}

bool UdpServer::Send(int fd, std::string host, uint16_t port, const std::string &str)
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    struct sockaddr_in remoteAddr;
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(port);
    inet_aton(host.c_str(), &remoteAddr.sin_addr);

    ssize_t bytes = sendto(socket_, str.c_str(), str.length(), 0, (struct sockaddr *)&remoteAddr, sizeof(remoteAddr));
    if (bytes != str.length()) {
        NETWORK_LOGE("send failed with error: %s, %zd/%zu", strerror(errno), bytes, str.length());
        return false;
    }

    return true;
}

void UdpServer::HandleReceive(int fd)
{
    if (fd != socket_) {
        NETWORK_LOGE("fd (%d) mismatch socket(%d)", fd, socket_);
        return;
    }

    if (readBuffer_ == nullptr) {
        readBuffer_ = std::make_unique<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    readBuffer_->Clear();
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    while (true) {
        ssize_t nbytes = recvfrom(socket_, readBuffer_->Data(), readBuffer_->Capacity(), 0,
                                  (struct sockaddr *)&clientAddr, &addrLen);
        std::string host = inet_ntoa(clientAddr.sin_addr);
        uint16_t port = ntohs(clientAddr.sin_port);
        NETWORK_LOGD("recvfrom %s:%d, size: %d", host.c_str(), port, (int)nbytes);

        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = std::make_shared<DataBuffer>(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);

                auto task = std::make_shared<TaskHandler<void>>([=]() {
                    auto listener = listener_.lock();
                    if (listener) {
                        auto session = std::make_shared<SessionImpl>(fd, host, port, shared_from_this());
                        listener->OnReceive(session, dataBuffer);
                    }
                });

                taskQueue_->EnqueueTask(task);
            }
            continue;
        }

        if (nbytes == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        std::string info = strerror(errno);
        if (!listener_.expired()) {
            auto task = std::make_shared<TaskHandler<void>>([=]() {
                auto listener = listener_.lock();
                if (listener) {
                    auto session = std::make_shared<SessionImpl>(fd, host, port, shared_from_this());
                    listener->OnError(session, info);
                } else {
                    NETWORK_LOGE("not found listener!");
                }
            });

            taskQueue_->EnqueueTask(task);
        }
        break;
    }
}

uint16_t UdpServer::GetIdlePort()
{
    int sock;
    struct sockaddr_in addr;

    uint16_t i;
    for (i = gIdlePort; i < gIdlePort + 100; i++) {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) {
            perror("socket creation failed");
            return -1;
        }

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(i);

        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            close(sock);
            continue;
        }

        close(sock);
        break;
    }

    if (i == gIdlePort + 100) {
        NETWORK_LOGE("Can't find idle port");
        return 0;
    }

    gIdlePort = i + 1;

    return i;
}

uint16_t UdpServer::GetIdlePortPair()
{
    uint16_t firstPort;
    uint16_t secondPort;
    firstPort = GetIdlePort();

    while (true) {
        secondPort = GetIdlePort();
        if (firstPort + 1 == secondPort || secondPort == 0) {
            return firstPort;
        } else {
            firstPort = secondPort;
        }
    }
}