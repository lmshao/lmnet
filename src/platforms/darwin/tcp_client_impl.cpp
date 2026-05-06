/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_client_impl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <mutex>
#include <queue>

#include "event_reactor.h"
#include "internal_logger.h"
#include "socket_utils.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;
using namespace darwin;

namespace {
const int RECV_BUFFER_MAX_SIZE = 4096;
}

class TcpClientHandler : public EventHandler {
public:
    TcpClientHandler(socket_t fd, std::weak_ptr<TcpClientImpl> client)
        : fd_(fd), client_(client), writeEventsEnabled_(false)
    {
    }

    void HandleRead(socket_t fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t) override { ProcessSendQueue(); }

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("Client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("Client connection close on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        int events =
            static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) | static_cast<int>(EventType::CLOSE);

        if (writeEventsEnabled_) {
            events |= static_cast<int>(EventType::WRITE);
        }

        return events;
    }

    void QueueSend(std::shared_ptr<DataBuffer> buffer)
    {
        if (!buffer || buffer->Size() == 0) {
            return;
        }

        bool shouldModify = false;
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            sendQueue_.push(buffer);
            if (!writeEventsEnabled_) {
                writeEventsEnabled_ = true;
                shouldModify = true;
            }
        }

        if (shouldModify) {
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
        }
    }

private:
    void ProcessSendQueue()
    {
        bool shouldModify = false;

        while (true) {
            std::shared_ptr<DataBuffer> buf;
            {
                std::lock_guard<std::mutex> lock(sendMutex_);
                if (sendQueue_.empty()) {
                    if (writeEventsEnabled_) {
                        writeEventsEnabled_ = false;
                        shouldModify = true;
                    }
                    break;
                }
                buf = sendQueue_.front();
            }

            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), 0);

            if (bytesSent > 0) {
                std::lock_guard<std::mutex> lock(sendMutex_);
                if (sendQueue_.empty()) {
                    continue;
                }

                if (static_cast<size_t>(bytesSent) == buf->Size()) {
                    sendQueue_.pop();
                } else {
                    auto remaining = DataBuffer::PoolAlloc(buf->Size() - bytesSent);
                    remaining->Assign(buf->Data() + bytesSent, buf->Size() - bytesSent);
                    sendQueue_.front() = remaining;
                    break;
                }
            } else if (bytesSent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }
                LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                return;
            }
        }

        if (shouldModify) {
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
        }
    }

private:
    socket_t fd_;
    std::weak_ptr<TcpClientImpl> client_;
    mutable std::mutex sendMutex_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

TcpClientImpl::TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpClientCb");
}

TcpClientImpl::~TcpClientImpl()
{
    Close();

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
}

bool TcpClientImpl::Init()
{
    isConnected_.store(false);

    socket_ = CreateStreamSocket(AF_INET);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket error: %s", strerror(errno));
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
        if (inet_pton(AF_INET, localIp_.c_str(), &localAddr.sin_addr) != 1) {
            LMNET_LOGE("invalid local IPv4 address: %s", localIp_.c_str());
            close(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        int optval = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            LMNET_LOGE("setsockopt SO_REUSEADDR error: %s", strerror(errno));
            close(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }

        if (bind(socket_, (struct sockaddr *)&localAddr, (socklen_t)sizeof(localAddr)) != 0) {
            LMNET_LOGE("bind error: %s", strerror(errno));
            close(socket_);
            socket_ = INVALID_SOCKET;
            return false;
        }
    }

    return true;
}

void TcpClientImpl::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
    isConnected_.store(false);
    Init();
}

bool TcpClientImpl::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized");
        return false;
    }

    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    if (remoteIp_.empty()) {
        remoteIp_ = "127.0.0.1";
    }

    if (inet_pton(AF_INET, remoteIp_.c_str(), &serverAddr_.sin_addr) != 1) {
        LMNET_LOGE("invalid remote IPv4 address: %s", remoteIp_.c_str());
        ReInit();
        return false;
    }

    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        LMNET_LOGE("connect(%s:%d) failed: %s", remoteIp_.c_str(), remotePort_, strerror(errno));
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
            LMNET_LOGE("getsockopt error, %s", strerror(errno));
            ReInit();
            return false;
        }

        if (error != 0) {
            LMNET_LOGE("connect error, %s", strerror(error));
            ReInit();
            return false;
        }
    } else {
        LMNET_LOGE("connect timeout or error, %s", strerror(errno));
        ReInit();
        return false;
    }

    if (taskQueue_ && taskQueue_->Start() != 0) {
        LMNET_LOGE("Failed to start task queue");
        Close();
        return false;
    }

    clientHandler_ = std::make_shared<TcpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(clientHandler_)) {
        LMNET_LOGE("Failed to register client handler");
        clientHandler_.reset();
        if (taskQueue_) {
            taskQueue_->Stop();
        }
        Close();
        return false;
    }

    isConnected_.store(true);

    LMNET_LOGD("Connect (%s:%d) success with kqueue backend", remoteIp_.c_str(), remotePort_);
    return true;
}

bool TcpClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("Invalid string data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(buf);
}

bool TcpClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        LMNET_LOGE("Invalid data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(len);
    buf->Assign(data, len);
    return Send(buf);
}

bool TcpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        LMNET_LOGE("Invalid data buffer");
        return false;
    }

    std::shared_ptr<TcpClientHandler> handler;
    {
        std::lock_guard<std::mutex> lock(closeMutex_);
        if (socket_ == INVALID_SOCKET) {
            LMNET_LOGE("socket not initialized");
            return false;
        }

        handler = clientHandler_;
    }

    if (handler) {
        handler->QueueSend(data);
        return true;
    }
    LMNET_LOGE("Client handler not found");
    return false;
}

void TcpClientImpl::Close()
{
    CloseInternal(socket_, false, "Closed by client", true);
}

void TcpClientImpl::HandleReceive(socket_t fd)
{
    {
        std::lock_guard<std::mutex> lock(closeMutex_);
        if (socket_ != fd) {
            return;
        }
    }

    LMNET_LOGD("fd: %d", fd);
    if (!readBuffer_) {
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
            LMNET_LOGW("Disconnect fd[%d]", fd);
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            std::string info = strerror(errno);
            LMNET_LOGE("recv error: %s(%d)", info.c_str(), errno);
            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void TcpClientImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
               isError ? "true" : "false");

    CloseInternal(fd, isError, reason, true);
}

void TcpClientImpl::CloseInternal(socket_t fd, bool isError, const std::string &reason, bool notifyListener)
{
    std::shared_ptr<TcpClientHandler> handler;
    socket_t socketToClose = INVALID_SOCKET;
    bool wasConnected = false;

    {
        std::lock_guard<std::mutex> lock(closeMutex_);
        if (socket_ == INVALID_SOCKET) {
            return;
        }

        if (fd != INVALID_SOCKET && socket_ != fd) {
            LMNET_LOGD("Connection fd: %d already cleaned up", fd);
            return;
        }

        socketToClose = socket_;
        socket_ = INVALID_SOCKET;
        handler = std::move(clientHandler_);
        wasConnected = isConnected_.exchange(false);
    }

    if (handler) {
        EventReactor::GetInstance().RemoveHandler(socketToClose);
    }
    close(socketToClose);

    if (notifyListener && wasConnected) {
        NotifyClose(socketToClose, isError, reason);
    }
}

void TcpClientImpl::NotifyClose(socket_t fd, bool isError, const std::string &reason)
{
    if (listener_.expired()) {
        return;
    }

    auto listenerWeak = listener_;
    auto task = std::make_shared<TaskHandler<void>>([listenerWeak, reason, isError, fd]() {
        auto listener = listenerWeak.lock();
        if (listener != nullptr) {
            if (isError) {
                listener->OnError(fd, reason);
            }
            listener->OnClose(fd);
        }
    });
    if (taskQueue_) {
        taskQueue_->EnqueueTask(task);
    }
}

} // namespace lmshao::lmnet
