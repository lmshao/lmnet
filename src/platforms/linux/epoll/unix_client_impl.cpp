/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "unix_client_impl.h"

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
#include "unix_socket_utils.h"

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

class UnixClientHandler : public EventHandler {
public:
    UnixClientHandler(socket_t fd, std::weak_ptr<UnixClientImpl> client)
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
        LMNET_LOGE("Unix client connection error on fd: %d", fd);
        if (auto client = client_.lock()) {
            client->HandleConnectionClose(fd, true, "Connection error");
        }
    }

    void HandleClose(socket_t fd) override
    {
        LMNET_LOGD("Unix client connection close on fd: %d", fd);
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
                    pending.offset = 1; // placeholder consumed
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
                if (auto client = client_.lock()) {
                    client->HandleConnectionClose(fd_, true, strerror(errno));
                }
                sendQueue_.pop();
                break;
            } else {
                // bytesSent == 0, treat as success and remove entry
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
    std::weak_ptr<UnixClientImpl> client_;
    std::queue<PendingSend> sendQueue_;
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
        LMNET_LOGE("Socket error: %s", strerror(errno));
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
        LMNET_LOGE("socket not initialized");
        return false;
    }

    int ret = connect(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_));
    if (ret < 0 && errno != EINPROGRESS) {
        LMNET_LOGE("connect(%s) failed: %s", socketPath_.c_str(), strerror(errno));
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
            return false;
        }

        if (error != 0) {
            LMNET_LOGE("connect error, %s", strerror(error));
            return false;
        }
    } else {
        LMNET_LOGE("connect timeout or error, %s", strerror(errno));
        return false;
    }

    taskQueue_->Start();

    clientHandler_ = std::make_shared<UnixClientHandler>(socket_, shared_from_this());
    if (!EventReactor::GetInstance().RegisterHandler(clientHandler_)) {
        LMNET_LOGE("Failed to register client handler");
        return false;
    }

    LMNET_LOGD("Connect (%s) success with new EventHandler interface.", socketPath_.c_str());
    return true;
}

bool UnixClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("Invalid string data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(str.size());
    buf->Assign(str.data(), str.size());
    return Send(buf);
}

bool UnixClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        LMNET_LOGE("Invalid data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(len);
    buf->Assign(data, len);
    return Send(buf);
}

bool UnixClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        LMNET_LOGE("Invalid data buffer");
        return false;
    }

    return SendWithFds(data, {});
}

bool UnixClientImpl::SendFds(const std::vector<int> &fds)
{
    if (fds.empty()) {
        LMNET_LOGE("No file descriptors provided");
        return false;
    }

    return SendWithFds(nullptr, fds);
}

bool UnixClientImpl::SendWithFds(std::shared_ptr<DataBuffer> data, const std::vector<int> &fds)
{
    if (!clientHandler_ || socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Cannot send data with file descriptors: not initialized or not connected");
        return false;
    }

    if ((!data || data->Size() == 0) && fds.empty()) {
        LMNET_LOGW("No data and no file descriptors to send");
        return true;
    }

    PendingSend pending;
    pending.data = data;

    // Duplicate file descriptors to avoid closing original descriptors
    if (!fds.empty()) {
        auto duplicatedFds = UnixSocketUtils::DuplicateFds(fds);
        if (duplicatedFds.empty()) {
            return false; // Failed to duplicate fds
        }
        pending.fds = std::move(duplicatedFds);
    }

    clientHandler_->QueueSend(std::move(pending));
    return true;
}

void UnixClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET && clientHandler_) {
        EventReactor::GetInstance().RemoveHandler(socket_);
        close(socket_);
        socket_ = INVALID_SOCKET;
        clientHandler_.reset();
    }
}

void UnixClientImpl::HandleReceive(socket_t fd)
{
    LMNET_LOGD("fd: %d", fd);
    if (readBuffer_ == nullptr) {
        readBuffer_ = DataBuffer::PoolAlloc(RECV_BUFFER_MAX_SIZE);
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
                        dataBuffer = DataBuffer::PoolAlloc(nbytes);
                        dataBuffer->Assign(readBuffer_->Data(), nbytes);
                    }
                }

                auto listenerWeak = listener_;
                auto task = std::make_shared<TaskHandler<void>>(
                    [listenerWeak, fd, dataBuffer, fds = std::move(receivedFds)]() mutable {
                        auto listener = listenerWeak.lock();
                        if (listener) {
                            UnixSocketUtils::ProcessClientMessage(listener, fd, dataBuffer, std::move(fds));
                        } else {
                            UnixSocketUtils::CleanupFds(fds);
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

            break;
        }
    }

    // exit while loop
}

void UnixClientImpl::HandleConnectionClose(socket_t fd, bool isError, const std::string &reason)
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
