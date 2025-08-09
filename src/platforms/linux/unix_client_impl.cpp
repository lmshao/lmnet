/**
 * @file unix_client_impl.cpp
 * @brief Unix Client Linux Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

// Unix domain sockets are only supported on Unix-like systems (Linux, macOS, BSD)
#if !defined(__unix__) && !defined(__unix) && !defined(unix) && !defined(__APPLE__)
#error "Unix domain sockets are not supported on this platform"
#endif

#include "unix_client_impl.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>

#include "event_reactor.h"
#include "log.h"

namespace lmshao::network {
constexpr int RECV_BUFFER_MAX_SIZE = 4096;

class UnixClientHandler : public EventHandler {
public:
    UnixClientHandler(int fd, std::weak_ptr<UnixClientImpl> client)
        : fd_(fd), client_(client), writeEventsEnabled_(false)
    {
    }

    void HandleRead(int fd) override
    {
        if (auto client = client_.lock()) {
            client->HandleReceive(fd);
        }
    }

    void HandleWrite(int fd) override { ProcessSendQueue(); }

    void HandleError(int fd) override
    {
        NETWORK_LOGE("Unix client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(int fd) override
    {
        NETWORK_LOGD("Unix client connection close on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, false, "Connection closed");
        }
    }

    int GetHandle() const override { return fd_; }

    int GetEvents() const override
    {
        int events =
            static_cast<int>(EventType::READ) | static_cast<int>(EventType::ERROR) | static_cast<int>(EventType::CLOSE);

        if (writeEventsEnabled_) {
            events |= static_cast<int>(EventType::WRITE);
        }

        return events;
    }

    void QueueSend(std::shared_ptr<DataBuffer> buffer)
    {
        if (!buffer || buffer->Size() == 0)
            return;
        sendQueue_.push(buffer);
        EnableWriteEvents();
    }

private:
    void EnableWriteEvents()
    {
        if (!writeEventsEnabled_) {
            writeEventsEnabled_ = true;
            EventReactor::GetInstance()->ModifyHandler(fd_, GetEvents());
        }
    }

    void DisableWriteEvents()
    {
        if (writeEventsEnabled_) {
            writeEventsEnabled_ = false;
            EventReactor::GetInstance()->ModifyHandler(fd_, GetEvents());
        }
    }

    void ProcessSendQueue()
    {
        while (!sendQueue_.empty()) {
            auto &buf = sendQueue_.front();
            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), MSG_NOSIGNAL);

            if (bytesSent > 0) {
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
                } else {
                    NETWORK_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                    return;
                }
            }
        }

        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }

private:
    int fd_;
    std::weak_ptr<UnixClientImpl> client_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

UnixClientImpl::UnixClientImpl(const std::string &socketPath) : socketPath_(socketPath)
{
    taskQueue_ = std::make_unique<TaskQueue>("UnixClientCb");
}

UnixClientImpl::~UnixClientImpl()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool UnixClientImpl::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("Socket error: %s", strerror(errno));
        return false;
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);

    return true;
}

bool UnixClientImpl::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        NETWORK_LOGE("connect(%s) failed: %s", socketPath_.c_str(), strerror(errno));
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
            return false;
        }

        if (error != 0) {
            NETWORK_LOGE("connect error, %s", strerror(error));
            return false;
        }
    } else {
        NETWORK_LOGE("connect timeout or error, %s", strerror(errno));
        return false;
    }

    taskQueue_->Start();

    clientHandler_ = std::make_shared<UnixClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance()->RegisterHandler(clientHandler_)) {
        NETWORK_LOGE("Failed to register client handler");
        return false;
    }

    NETWORK_LOGD("Connect (%s) success with new EventHandler interface.", socketPath_.c_str());
    return true;
}

bool UnixClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        NETWORK_LOGE("Invalid string data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(buf);
}

bool UnixClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        NETWORK_LOGE("Invalid data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(len);
    buf->Assign(data, len);
    return Send(buf);
}

bool UnixClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        NETWORK_LOGE("Invalid data buffer");
        return false;
    }

    if (socket_ == INVALID_SOCKET) {
        NETWORK_LOGE("socket not initialized");
        return false;
    }

    if (clientHandler_) {
        clientHandler_->QueueSend(data);
        return true;
    }
    NETWORK_LOGE("Client handler not found");
    return false;
}

void UnixClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance()->RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

void UnixClientImpl::HandleReceive(int fd)
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

void UnixClientImpl::HandleConnectionClose(int fd, bool isError, const std::string &reason)
{
    NETWORK_LOGD("Closing client connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(),
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
} // namespace lmshao::network
