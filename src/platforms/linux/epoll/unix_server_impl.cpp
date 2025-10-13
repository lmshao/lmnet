/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "unix_server_impl.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <queue>
#include <utility>
#include <vector>

#include "event_reactor.h"
#include "internal_logger.h"
#include "unix_session_impl.h"

namespace lmshao::lmnet {

using lmshao::lmcore::TaskHandler;

constexpr int RECV_BUFFER_MAX_SIZE = 4096;
constexpr size_t MAX_FDS_PER_MESSAGE = 16;

struct PendingSend {
    std::shared_ptr<DataBuffer> data;
    size_t offset = 0;
    std::vector<int> fds;
    bool fdsSent = false;
};

class UnixServerHandler : public EventHandler {
public:
    explicit UnixServerHandler(std::weak_ptr<UnixServerImpl> server) : server_(server) {}

    void HandleRead(socket_t fd) override
    {
        if (auto server = server_.lock()) {
            server->HandleAccept(fd);
        }
    }

    void HandleWrite(int) override {}

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

    void HandleWrite(socket_t fd) override { ProcessSendQueue(); }

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

    void QueueSend(PendingSend pending)
    {
        if ((!pending.data || pending.data->Size() == 0) && pending.fds.empty()) {
            return;
        }
        sendQueue_.push(std::move(pending));
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
            auto &pending = sendQueue_.front();

            std::array<char, 1> placeholder{{0}};
            struct iovec iov;
            if (pending.data && pending.data->Size() > pending.offset) {
                iov.iov_base = pending.data->Data() + pending.offset;
                iov.iov_len = pending.data->Size() - pending.offset;
            } else {
                iov.iov_base = placeholder.data();
                iov.iov_len = placeholder.size();
            }

            std::vector<char> controlBuffer;
            struct msghdr msg = {};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1;

            if (!pending.fds.empty() && !pending.fdsSent) {
                controlBuffer.resize(CMSG_SPACE(sizeof(int) * pending.fds.size()));
                msg.msg_control = controlBuffer.data();
                msg.msg_controllen = controlBuffer.size();

                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type = SCM_RIGHTS;
                cmsg->cmsg_len = CMSG_LEN(sizeof(int) * pending.fds.size());
                std::memcpy(CMSG_DATA(cmsg), pending.fds.data(), sizeof(int) * pending.fds.size());
            }

            ssize_t bytesSent = sendmsg(fd_, &msg, MSG_NOSIGNAL);
            if (bytesSent > 0) {
                pending.fdsSent = true;
                CloseDescriptors(pending.fds);

                size_t sentBytes = static_cast<size_t>(bytesSent);
                if (pending.data && pending.data->Size() > pending.offset) {
                    pending.offset += sentBytes;
                    if (pending.offset >= pending.data->Size()) {
                        pending.data.reset();
                    }
                } else {
                    pending.offset = 1;
                }

                if (!pending.data || (pending.data->Size() <= pending.offset)) {
                    sendQueue_.pop();
                } else {
                    break;
                }
            } else if (bytesSent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                }

                LMNET_LOGE("Send error on fd %d: %s", fd_, strerror(errno));
                CloseDescriptors(pending.fds);
                if (auto server = server_.lock()) {
                    server->HandleConnectionClose(fd_, true, strerror(errno));
                }
                sendQueue_.pop();
                break;
            } else {
                pending.fdsSent = true;
                CloseDescriptors(pending.fds);
                sendQueue_.pop();
            }
        }

        if (sendQueue_.empty()) {
            DisableWriteEvents();
        }
    }

    static void CloseDescriptors(std::vector<int> &fds)
    {
        for (int descriptor : fds) {
            if (descriptor >= 0) {
                ::close(descriptor);
            }
        }
        fds.clear();
    }

private:
    socket_t fd_;
    std::weak_ptr<UnixServerImpl> server_;
    std::queue<PendingSend> sendQueue_;
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
    socket_ = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket error: %s", strerror(errno));
        return false;
    }
    LMNET_LOGD("init path: %s, fd:%d", socketPath_.c_str(), socket_);

    // Remove existing socket file if it exists
    unlink(socketPath_.c_str());

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
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

    taskQueue_->Start();

    serverHandler_ = std::make_shared<UnixServerHandler>(shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(serverHandler_)) {
        LMNET_LOGE("Failed to register server handler");
        return false;
    }

    LMNET_LOGD("UnixServerImpl started with new EventHandler interface");
    return true;
}

bool UnixServerImpl::Stop()
{
    auto &reactor = EventReactor::GetInstance();

    std::vector<int> clientFds;
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
            PendingSend pending;
            pending.data = std::move(buffer);
            unixHandler->QueueSend(std::move(pending));
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
    if (fds.empty()) {
        LMNET_LOGE("No file descriptors provided");
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        LMNET_LOGE("invalid session fd");
        return false;
    }

    std::vector<int> duplicatedFds;
    duplicatedFds.reserve(fds.size());
    for (int descriptor : fds) {
        if (descriptor < 0) {
            LMNET_LOGE("Invalid file descriptor: %d", descriptor);
            continue;
        }
        int duplicated = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
        if (duplicated < 0) {
            LMNET_LOGE("Failed to duplicate fd %d: %s", descriptor, strerror(errno));
            for (int dupFd : duplicatedFds) {
                ::close(dupFd);
            }
            return false;
        }
        duplicatedFds.push_back(duplicated);
    }

    if (duplicatedFds.empty()) {
        LMNET_LOGE("No valid file descriptors duplicated");
        return false;
    }

    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            PendingSend pending;
            pending.fds = std::move(duplicatedFds);
            unixHandler->QueueSend(std::move(pending));
            return true;
        }
    }

    for (int dupFd : duplicatedFds) {
        ::close(dupFd);
    }
    return false;
}

bool UnixServerImpl::SendWithFds(socket_t fd, std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds)
{
    if ((!buffer || buffer->Size() == 0) && fds.empty()) {
        LMNET_LOGE("No data or file descriptors provided");
        return false;
    }

    if (sessions_.find(fd) == sessions_.end()) {
        LMNET_LOGE("Invalid session fd");
        return false;
    }

    std::vector<int> duplicatedFds;
    if (!fds.empty()) {
        duplicatedFds.reserve(fds.size());
        for (int descriptor : fds) {
            if (descriptor < 0) {
                LMNET_LOGE("Invalid file descriptor: %d", descriptor);
                continue;
            }
            int duplicated = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
            if (duplicated < 0) {
                LMNET_LOGE("Failed to duplicate fd %d: %s", descriptor, strerror(errno));
                for (int dupFd : duplicatedFds) {
                    ::close(dupFd);
                }
                return false;
            }
            duplicatedFds.push_back(duplicated);
        }

        if (duplicatedFds.empty() && (!buffer || buffer->Size() == 0)) {
            LMNET_LOGE("No valid file descriptors duplicated and no data");
            return false;
        }
    }

    auto handlerIt = connectionHandlers_.find(fd);
    if (handlerIt != connectionHandlers_.end()) {
        auto unixHandler = handlerIt->second;
        if (unixHandler) {
            PendingSend pending;
            pending.data = std::move(buffer);
            pending.fds = std::move(duplicatedFds);
            unixHandler->QueueSend(std::move(pending));
            return true;
        }
    }

    // Clean up duplicated file descriptors if send failed
    for (int dupFd : duplicatedFds) {
        ::close(dupFd);
    }

    LMNET_LOGE("Connection handler not found for fd: %d", fd);
    return false;
}

void UnixServerImpl::HandleAccept(socket_t fd)
{
    LMNET_LOGD("enter");
    struct sockaddr_un clientAddr = {};
    socklen_t addrLen = sizeof(struct sockaddr_un);
    int clientSocket = accept4(fd, (struct sockaddr *)&clientAddr, &addrLen, SOCK_NONBLOCK);
    if (clientSocket < 0) {
        LMNET_LOGE("accept error: %s", strerror(errno));
        return;
    }

    auto connectionHandler = std::make_shared<UnixConnectionHandler>(clientSocket, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(connectionHandler)) {
        LMNET_LOGE("Failed to register connection handler for fd: %d", clientSocket);
        close(clientSocket);
        return;
    }

    connectionHandlers_[clientSocket] = connectionHandler;

    LMNET_LOGD("New Unix client connection client[%d]\n", clientSocket);

    // Unix domain socket uses empty host and port
    auto session = std::make_shared<UnixSessionImpl>(clientSocket, socketPath_, shared_from_this());
    sessions_.emplace(clientSocket, session);

    if (!listener_.expired()) {
        auto listenerWeak = listener_;
        auto sessionPtr = sessions_[clientSocket];
        auto task = std::make_shared<TaskHandler<void>>([listenerWeak, sessionPtr]() {
            LMNET_LOGD("invoke OnAccept callback");
            auto listener = listenerWeak.lock();
            if (listener) {
                listener->OnAccept(sessionPtr);
            } else {
                LMNET_LOGD("not found listener!");
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
        readBuffer_ = std::make_shared<DataBuffer>(RECV_BUFFER_MAX_SIZE);
    }

    while (true) {
        std::array<char, CMSG_SPACE(sizeof(int) * MAX_FDS_PER_MESSAGE)> controlBuffer{};
        struct iovec iov;
        iov.iov_base = readBuffer_->Data();
        iov.iov_len = readBuffer_->Capacity();

        struct msghdr msg = {};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = controlBuffer.data();
        msg.msg_controllen = controlBuffer.size();

        ssize_t nbytes = recvmsg(fd, &msg, MSG_DONTWAIT);

        if (nbytes > 0) {
            if (msg.msg_flags & MSG_CTRUNC) {
                LMNET_LOGE("Ancillary data truncated on recvmsg");
            }

            if (!listener_.expired()) {
                std::vector<int> receivedFds;

                for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
                    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                        size_t fdCount = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                        if (fdCount > 0) {
                            int *fds = reinterpret_cast<int *>(CMSG_DATA(cmsg));
                            receivedFds.insert(receivedFds.end(), fds, fds + fdCount);
                        }
                    }
                }

                std::shared_ptr<DataBuffer> dataBuffer;
                if (nbytes > 0) {
                    bool isPlaceholder = !receivedFds.empty() && nbytes == 1 && readBuffer_->Data()[0] == 0;
                    if (!isPlaceholder) {
                        dataBuffer = std::make_shared<DataBuffer>(nbytes);
                        dataBuffer->Assign(readBuffer_->Data(), nbytes);
                    }
                }

                auto sessionIt = sessions_.find(fd);
                if (sessionIt != sessions_.end()) {
                    auto session = sessionIt->second;
                    auto listenerWeak = listener_;
                    auto task = std::make_shared<TaskHandler<void>>(
                        [listenerWeak, session, dataBuffer, fds = std::move(receivedFds)]() mutable {
                            auto listener = listenerWeak.lock();
                            if (listener != nullptr) {
                                if (dataBuffer) {
                                    listener->OnReceive(session, dataBuffer);
                                }
                                if (!fds.empty()) {
                                    listener->OnReceiveFds(session, std::move(fds));
                                }
                            } else {
                                for (int descriptor : fds) {
                                    if (descriptor >= 0) {
                                        ::close(descriptor);
                                    }
                                }
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
            // Do not call HandleConnectionClose directly; let the event system handle EPOLLHUP
            break;
        } else {
            if (errno == EAGAIN) {
                break;
            }

            std::string info = strerror(errno);
            LMNET_LOGE("recv error: %s(%d)", info.c_str(), errno);

            if (errno == ETIMEDOUT) {
                LMNET_LOGE("ETIME: connection is timeout");
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
