/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "unix_server_impl.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <queue>
#include <vector>

#include "event_reactor.h"
#include "internal_logger.h"
#include "socket_utils.h"
#include "unix_session_impl.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;
using namespace darwin;

namespace {
constexpr int RECV_BUFFER_MAX_SIZE = 4096;
}

class UnixServerHandler : public EventHandler {
public:
    explicit UnixServerHandler(std::weak_ptr<UnixServerImpl> server) : server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(socket_t) override {}

    void HandleError(socket_t fd) override { LMNET_LOGE("Unix server socket error on fd: %d", fd); }

    void HandleClose(socket_t fd) override { LMNET_LOGD("Unix server socket close on fd: %d", fd); }

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
    std::weak_ptr<UnixServerImpl> server_;
};

class UnixConnectionHandler : public EventHandler {
public:
    UnixConnectionHandler(socket_t fd, std::weak_ptr<UnixServerImpl> server)
        : fd_(fd), server_(server), writeEventsEnabled_(false)
    {
    }

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleReceive(fd);
        }
    }

    void HandleWrite(socket_t) override { ProcessSendQueue(); }

    void HandleError(socket_t fd) override
    {
        LMNET_LOGE("Unix connection error on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("Unix connection close on fd: %d", fd);
        if (auto server = server_.lock()) {
            server->HandleConnectionClose(fd, false, "Connection closed");
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
        if (!buffer || buffer->Size() == 0) {
            return;
        }
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
            ssize_t bytesSent = send(fd_, buf->Data(), buf->Size(), 0);

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
                }
                LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                return;
            } else {
                break;
            }
        }

        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }

private:
    socket_t fd_;
    std::weak_ptr<UnixServerImpl> server_;
    std::queue<std::shared_ptr<DataBuffer>> sendQueue_;
    bool writeEventsEnabled_;
};

UnixServerImpl::UnixServerImpl(const std::string &socketPath) : socketPath_(socketPath) {}

UnixServerImpl::~UnixServerImpl()
{
    LMNET_LOGD("fd:%d", socket_);
    Stop();
}

bool UnixServerImpl::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket error: %s", strerror(errno));
        return false;
    }

    if (!ConfigureAcceptedSocket(socket_)) {
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    LMNET_LOGD("init path: %s, fd:%d", socketPath_.c_str(), socket_);

    // Remove existing socket file if it exists
    unlink(socketPath_.c_str());

    std::memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    std::strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);

    if (bind(socket_, reinterpret_cast<struct sockaddr *>(&serverAddr_), sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    if (listen(socket_, 10) < 0) {
        LMNET_LOGE("listen error: %s", strerror(errno));
        return false;
    }

    taskQueue_ = std::make_unique<TaskQueue>("UnixServerCb");
    return true;
}

bool UnixServerImpl::Start()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized");
        return false;
    }

    if (taskQueue_) {
        taskQueue_->Start();
    }

    serverHandler_ = std::make_shared<UnixServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(serverHandler_)) {
        LMNET_LOGE("Failed to register server handler");
        return false;
    }

    LMNET_LOGD("UnixServerImpl started with EventHandler interface");
    return true;
}

bool UnixServerImpl::Stop()
{
    auto &reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
    clientFds.reserve(sessions_.size());
    for (const auto &pair : sessions_) {
        clientFds.push_back(pair.first);
    }

    for (int clientFd : clientFds) {
        LMNET_LOGD("close client fd: %d", clientFd);
        reactor.RemoveHandler(clientFd);
        close(clientFd);
        connectionHandlers_.erase(clientFd);
    }
    sessions_.clear();

    if (socket_ != INVALID_SOCKET && serverHandler_) {
        LMNET_LOGD("close server fd: %d", socket_);
        reactor.RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        serverHandler_.reset();
    }

    if (taskQueue_) {
        taskQueue_->Stop();
        taskQueue_.reset();
    }

    // Remove socket file
    unlink(socketPath_.c_str());

    LMNET_LOGD("UnixServerImpl stopped");
    return true;
}

bool UnixServerImpl::Send(socket_t fd, const void *data, size_t size)
{
    if (!data || size == 0) {
        LMNET_LOGE("invalid data or size");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(size);
    buf->Assign(reinterpret_cast<const char *>(data), size);
    return Send(fd, std::move(buf));
}

bool UnixServerImpl::Send(socket_t fd, std::shared_ptr<DataBuffer> buffer)
{
    if (!buffer || buffer->Size() == 0) {
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        LMNET_LOGE("invalid session fd");
        return false;
    }

    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            unixHandler->QueueSend(buffer);
            return true;
        }
    }
    LMNET_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

bool UnixServerImpl::Send(socket_t fd, const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("invalid string data");
        return false;
    }
    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(fd, std::move(buf));
}

bool UnixServerImpl::SendFds(socket_t fd, const std::vector<int> &fds)
{
    (void)fd;
    (void)fds;
    LMNET_LOGE("SendFds is not supported on macOS backend yet");
    return false;
}

bool UnixServerImpl::SendWithFds(socket_t fd, std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds)
{
    (void)fd;
    (void)buffer;
    (void)fds;
    LMNET_LOGE("SendWithFds is not supported on macOS backend yet");
    return false;
}

void UnixServerImpl::HandleAccept(socket_t fd)
{
    LMNET_LOGD("enter");
    struct sockaddr_un clientAddr = {};
    socklen_t addrLen = sizeof(struct sockaddr_un);
    int clientSocket = accept(fd, reinterpret_cast<struct sockaddr *>(&clientAddr), &addrLen);
    if (clientSocket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LMNET_LOGE("accept error: %s", strerror(errno));
        }
        return;
    }

    if (!ConfigureAcceptedSocket(clientSocket)) {
        close(clientSocket);
        return;
    }

    auto connectionHandler = std::make_shared<UnixConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(connectionHandler)) {
        LMNET_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    connectionHandlers_[clientSocket] = connectionHandler;

    LMNET_LOGD("New Unix client connection client[%d]", clientSocket);

    // Unix domain socket uses empty host and port
    auto session = std::make_shared<UnixSessionImpl>(clientSocket, socketPath_, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto sessionPtr = sessions_[clientSocket];
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, sessionPtr]() {
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(sessionPtr);
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    } else {
        LMNET_LOGD("listener is null");
    }
}

void UnixServerImpl::HandleReceive(socket_t fd)
{
    LMNET_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        ssize_t nbytes = recv(fd, readBuffer_->Data(), readBuffer_->Capacity(), MSG_DONTWAIT);

        if (nbytes > 0) {
            if (static_cast<size_t>(nbytes) > readBuffer_->Capacity()) {
                LMNET_LOGE("recv %zd bytes exceeds buffer", nbytes);
                break;
            }

            if (!listener_.expired()) {
                auto dataBuffer = DataBuffer::PoolAlloc(nbytes);
                dataBuffer->Assign(readBuffer_->Data(), nbytes);

                std::shared_ptr<Session> session;
                auto it = sessions_.find(fd);
                if (it != sessions_.end()) {
                    session = it->second;
                }

                if (session) {
                    auto listenerWeak = listener_;
                    auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session, dataBuffer]() {
                        auto listener = listenerWeak.lock();
                        if (listener != nullptr) {
                            listener->OnReceive(session, dataBuffer);
                        }
                    });
                    if (taskQueue_) {
                        taskQueue_->EnqueueTask(task);
                    }
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

            if (errno == ETIMEDOUT) {
                LMNET_LOGE("ETIME: connection timeout");
                break;
            }

            HandleConnectionClose(fd, true, info);
        }

        break;
    }
}

void UnixServerImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
{
    LMNET_LOGD("Closing connection fd: %d, reason: %s, isError: %s", fd, reason.c_str(), isError ? "true" : "false");

    auto sessionIt = sessions_.find(fd);
    if (sessionIt == sessions_.end()) {
        LMNET_LOGD("Connection fd: %d already cleaned up", fd);
        return;
    }

    EventReactor::GetInstance().RemoveHandler(fd);

    close(fd);

    std::shared_ptr<Session> session = sessionIt->second;
    sessions_.erase(sessionIt);

    connectionHandlers_.erase(fd);

    if (!listener_.expired() && session) {
        auto listenerWeak = listener_;
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, session, reason, isError]() {
            auto listener = listenerWeak.lock();
            if (listener != nullptr) {
                if (isError) {
                    listener->OnError(session, reason);
                } else {
                    listener->OnClose(session);
                }
            }
        });
        if (taskQueue_) {
            taskQueue_->EnqueueTask(task);
        }
    }
}

} // namespace lmshao::lmnet
