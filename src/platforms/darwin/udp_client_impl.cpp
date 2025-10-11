/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "event_reactor.h"
#include "internal_logger.h"
#include "socket_utils.h"

namespace lmshao::lmnet {

using namespace darwin;

namespace {
constexpr int RECV_BUFFER_MAX_SIZE = 4096;
}

class UdpClientHandler : public EventHandler {
public:
    UdpClientHandler(socket_t fd, std::weak_ptr<UdpClientImpl> client) : fd_(fd), client_(std::move(client)) {}

    void HandleRead(socket_t fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t) override {}

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("UDP client error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("UDP client closed on fd: %d", fd);
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
    socket_t fd_;
    std::weak_ptr<UdpClientImpl> client_;
};

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<lmcore::TaskQueue>("UdpClientCb");
}

UdpClientImpl::~UdpClientImpl()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool UdpClientImpl::Init()
{
    socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket creation failed: %s", strerror(errno));
        return false;
    }

    if (!SetNonBlocking(socket_) || !SetCloseOnExec(socket_)) {
        LMNET_LOGE("Failed to configure UDP client socket: %s", strerror(errno));
        Close();
        return false;
    }

    DisableSigpipe(socket_);

    struct sockaddr_in remoteAddr {};
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port = htons(remotePort_);
    if (inet_pton(AF_INET, remoteIp_.c_str(), &remoteAddr.sin_addr) <= 0) {
        LMNET_LOGE("inet_pton failed for %s", remoteIp_.c_str());
        Close();
        return false;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        struct sockaddr_in localAddr {};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        if (inet_pton(AF_INET, localIp_.c_str(), &localAddr.sin_addr) <= 0) {
            LMNET_LOGE("inet_pton failed for local IP %s", localIp_.c_str());
            Close();
            return false;
        }

        if (bind(socket_, reinterpret_cast<struct sockaddr *>(&localAddr), sizeof(localAddr)) < 0) {
            LMNET_LOGE("bind failed: %s", strerror(errno));
            Close();
            return false;
        }
    }

    // Connect the UDP socket to simplify send/recv operations
    if (connect(socket_, reinterpret_cast<struct sockaddr *>(&remoteAddr), sizeof(remoteAddr)) < 0) {
        LMNET_LOGE("connect failed: %s", strerror(errno));
        Close();
        return false;
    }

    if (taskQueue_->Start() != 0) {
        LMNET_LOGE("Failed to start UDP client task queue");
        Close();
        return false;
    }

    clientHandler_ = std::make_shared<UdpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(clientHandler_)) {
        LMNET_LOGE("Failed to register UDP client handler");
        Close();
        return false;
    }

    LMNET_LOGD("UDP client initialized for %s:%u", remoteIp_.c_str(), remotePort_);
    return true;
}

bool UdpClientImpl::EnableBroadcast()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized");
        return false;
    }

    int broadcast = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast)) < 0) {
        LMNET_LOGE("Failed to enable broadcast: %s", strerror(errno));
        return false;
    }

    return true;
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket not initialized");
        return false;
    }

    if (!data || len == 0) {
        LMNET_LOGE("Invalid send parameters: data=%p len=%zu", data, len);
        return false;
    }

    ssize_t nbytes = send(socket_, data, len, 0);
    if (nbytes < 0) {
        LMNET_LOGE("send failed: %s", strerror(errno));
        return false;
    }

    if (static_cast<size_t>(nbytes) != len) {
        LMNET_LOGW("Partial UDP send: %zd of %zu bytes", nbytes, len);
    }

    return true;
}

bool UdpClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("Cannot send empty string");
        return false;
    }
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(std::shared_ptr<lmcore::DataBuffer> data)
{
    if (!data) {
        LMNET_LOGE("Data buffer is null");
        return false;
    }
    return Send(data->Data(), data->Size());
}

void UdpClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET) {
        if (clientHandler_) {
            EventReactor::GetInstance().RemoveHandler(socket_);
            clientHandler_.reset();
        }
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
}

void UdpClientImpl::HandleReceive(socket_t fd)
{
    if (!readBuffer_) {
        readBuffer_ = lmcore::DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);
        if (nbytes > 0) {
            if (!listener_.expired()) {
                auto dataBuffer = lmcore::DataBuffer::PoolAlloc(static_cast<size_t>(nbytes));
                dataBuffer->Assign(readBuffer_->Data(), static_cast<size_t>(nbytes));
                auto listenerWeak = listener_;
                auto task = std::make_shared<lmcore::TaskHandler<void>>([listenerWeak, dataBuffer, fd]() {
                    if (auto listener = listenerWeak.lock()) {
                        listener->OnReceive(fd, dataBuffer);
                    }
                });
                if (taskQueue_) {
                    taskQueue_->EnqueueTask(task);
                }
            }
            continue;
        }

        if (nbytes == 0) {
            HandleConnectionClose(fd, false, "Peer closed");
            break;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        std::string reason = strerror(errno);
        HandleConnectionClose(fd, true, reason);
        break;
    }
}

void UdpClientImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing UDP client fd %d reason %s isError %s", fd, reason.c_str(), isError ? "true" : "false");

    if (socket_ != fd) {
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);
    close(fd);
    socket_ = INVALID_SOCKET;
    clientHandler_.reset();

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<lmcore::TaskHandler<void>>([listenerWeak, reason, isError, fd]() {
            if (auto listener = listenerWeak.lock()) {
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

} // namespace lmshao::lmnet
