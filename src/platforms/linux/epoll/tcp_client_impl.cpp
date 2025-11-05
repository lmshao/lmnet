/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_client_impl.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>

#include "event_reactor.h"
#include "internal_logger.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskHandler;

const int RECV_BUFFER_MAX_SIZE = 4096;

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

    void HandleWrite(socket_t fd) override { ProcessSendQueue(); }

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
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
        }
    }

    void DisableWriteEvents()
    {
        if (writeEventsEnabled_) {
            writeEventsEnabled_ = false;
            EventReactor::GetInstance().ModifyHandler(fd_, GetEvents());
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
                    LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                    return;
                }
            }
        }

        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }

private:
    socket_t fd_;
    std::weak_ptr<TcpClientImpl> client_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

TcpClientImpl::TcpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(remoteIp), remotePort_(remotePort), localIp_(localIp), localPort_(localPort)
{
    taskQueue_ = std::make_unique<TaskQueue>("TcpClientCb");
}

TcpClientImpl::~TcpClientImpl()
{
    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }
    Close();
}

bool TcpClientImpl::Init()
{
    // Resolve remote and create non-blocking socket here (single-stack)
    if (remoteIp_.empty()) {
        remoteIp_ = "127.0.0.1";
    }

    char port_str[16] = {0};
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(remotePort_));

    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;

    struct addrinfo *result = nullptr;
    int rv = getaddrinfo(remoteIp_.c_str(), port_str, &hints, &result);
    if (rv != 0 || result == nullptr) {
        LMNET_LOGE("getaddrinfo failed: %s", gai_strerror(rv));
        return false;
    }

    struct addrinfo *picked = result; // single-stack: use the first candidate
    int s = ::socket(picked->ai_family, picked->ai_socktype | SOCK_NONBLOCK, picked->ai_protocol);
    if (s < 0) {
        LMNET_LOGE("socket create failed: %s", strerror(errno));
        freeaddrinfo(result);
        return false;
    }

    // Optional local bind using the same family
    if (!localIp_.empty() || localPort_ != 0) {
        struct addrinfo lhints {};
        memset(&lhints, 0, sizeof(lhints));
        lhints.ai_family = picked->ai_family;
        lhints.ai_socktype = SOCK_STREAM;
        lhints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;

        char lport[16] = {0};
        snprintf(lport, sizeof(lport), "%u", static_cast<unsigned>(localPort_));

        const char *lip = localIp_.empty() ? nullptr : localIp_.c_str();
        struct addrinfo *lres = nullptr;
        int lrv = getaddrinfo(lip, lport, &lhints, &lres);
        if (lrv == 0 && lres) {
            int reuse = 1;
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            if (bind(s, lres->ai_addr, lres->ai_addrlen) != 0) {
                LMNET_LOGE("bind local failed: %s", strerror(errno));
            }
            freeaddrinfo(lres);
        }
    }

    // Cache server addr and keep socket for Connect
    memset(&server_addr_, 0, sizeof(server_addr_));
    memcpy(&server_addr_, picked->ai_addr, picked->ai_addrlen);
    socket_ = s;
    freeaddrinfo(result);

    LMNET_LOGD("TCP client initialized single-stack for %s:%u (fd=%d)", remoteIp_.c_str(),
               static_cast<unsigned>(remotePort_), socket_);
    return true;
}

void TcpClientImpl::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
    Init();
}

bool TcpClientImpl::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized; call Init() first");
        return false;
    }

    // Non-blocking connect on prepared socket/server_addr_
    int ret = connect(socket_, reinterpret_cast<sockaddr *>(&server_addr_), sizeof(server_addr_));
    if (ret < 0 && errno != EINPROGRESS) {
        LMNET_LOGE("connect attempt failed: %s", strerror(errno));
        Close();
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
    if (ret <= 0) {
        LMNET_LOGE("connect select error, %s", strerror(errno));
        Close();
        ReInit();
        return false;
    }

    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
        LMNET_LOGE("connect error, %s", strerror(error != 0 ? error : errno));
        Close();
        ReInit();
        return false;
    }

    taskQueue_->Start();

    clientHandler_ = std::make_shared<TcpClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(clientHandler_)) {
        LMNET_LOGE("Failed to register client handler");
        return false;
    }

    char hostbuf[INET6_ADDRSTRLEN]{};
    uint16_t port = 0;
    if (server_addr_.ss_family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&server_addr_);
        inet_ntop(AF_INET, &sin->sin_addr, hostbuf, sizeof(hostbuf));
        port = ntohs(sin->sin_port);
    } else if (server_addr_.ss_family == AF_INET6) {
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(&server_addr_);
        inet_ntop(AF_INET6, &sin6->sin6_addr, hostbuf, sizeof(hostbuf));
        port = ntohs(sin6->sin6_port);
    }
    LMNET_LOGD("Connect (%s:%u) success with new EventHandler interface.", hostbuf, static_cast<unsigned>(port));
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

    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized");
        return false;
    }

    if (clientHandler_) {
        clientHandler_->QueueSend(data);
        return true;
    }
    LMNET_LOGE("Client handler not found");
    return false;
}

void TcpClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance().RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

void TcpClientImpl::HandleReceive(socket_t fd)
{
    LMNET_LOGD("fd: %d", fd);
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
            LMNET_LOGW("Disconnect fd[%d]", fd);
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) { // Usually same value, but check both for portability
                // Normal case: no data available to read, return directly
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

    if (socket_ != fd) {
        LMNET_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);
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
} // namespace lmshao::lmnet