#include "unix_client_impl.h"

#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <vector>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "lmnet/unix_message.h"
#include "unix_socket_utils.h"

namespace lmshao::lmnet {

UnixClientImpl::UnixClientImpl(const std::string &socketPath) : socketPath_(socketPath) {}

UnixClientImpl::~UnixClientImpl()
{
    Close();
}

bool UnixClientImpl::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    isRunning_ = true;
    return true;
}

bool UnixClientImpl::Connect()
{
    if (!isRunning_)
        return false;

    sockaddr_un server_addr{};
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, socketPath_.c_str(), sizeof(server_addr.sun_path) - 1);

    // Use synchronous connect like epoll implementation to maintain interface compatibility
    int ret = connect(socket_, reinterpret_cast<const sockaddr *>(&server_addr), sizeof(server_addr));
    if (ret < 0 && errno != EINPROGRESS) {
        LMNET_LOGE("connect(%s) failed: %s", socketPath_.c_str(), strerror(errno));
        return false;
    }

    if (errno == EINPROGRESS) {
        // Wait for connection to complete
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
    }

    isConnected_ = true;
    LMNET_LOGI("Unix client connected to %s", socketPath_.c_str());

    // Start receiving data
    if (!StartReceive()) {
        HandleClose();
        return false;
    }

    return true;
}

void UnixClientImpl::HandleConnect(int result)
{
    if (result >= 0) {
        isConnected_ = true;
        LMNET_LOGI("Unix client connected to %s", socketPath_.c_str());
        // Connection is successful, start receiving data.
        // The listener will be notified via OnReceive.
        StartReceive();
    } else {
        LMNET_LOGE("Failed to connect to %s: %s", socketPath_.c_str(), strerror(-result));
        HandleClose();
    }
}

bool UnixClientImpl::StartReceive()
{
    if (!isRunning_ || !isConnected_)
        return false;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    // Use SubmitRecvMsgRequest to receive both data and file descriptors
    return IoUringManager::GetInstance().SubmitRecvMsgRequest(
        socket_, buffer, [self](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read, std::vector<int> fds) {
            self->HandleReceiveWithFds(buf, bytes_read, std::move(fds));
        });
}

void UnixClientImpl::HandleReceiveWithFds(std::shared_ptr<DataBuffer> buffer, int bytes_read, std::vector<int> fds)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            UnixSocketUtils::ProcessClientMessage(listener, socket_, buffer, std::move(fds));
        } else {
            UnixSocketUtils::CleanupFds(fds);
        }
        StartReceive(); // Continue receiving
    } else if (bytes_read == 0) {
        // Clean up any received file descriptors if connection closed
        UnixSocketUtils::CleanupFds(fds);
        LMNET_LOGI("Connection closed by peer.");
        HandleClose();
    } else {
        // Clean up any received file descriptors on error
        UnixSocketUtils::CleanupFds(fds);

        // Handle different error types
        if (bytes_read == -EAGAIN || bytes_read == -EWOULDBLOCK) {
            // Non-blocking I/O would block - this is normal, retry
            StartReceive();
        } else if (isRunning_) {
            // Other errors - log and close
            LMNET_LOGE("Read failed: %s", strerror(-bytes_read));
            if (auto listener = listener_.lock()) {
                listener->OnError(socket_, strerror(-bytes_read));
            }
            HandleClose();
        }
    }
}

bool UnixClientImpl::Send(const void *data, size_t len)
{
    auto buffer = std::make_shared<DataBuffer>();
    buffer->Assign(data, len);
    return Send(buffer);
}

bool UnixClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool UnixClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!isRunning_ || !isConnected_)
        return false;

    if (!data || data->Size() == 0) {
        return false;
    }

    return QueueStreamWrite(std::move(data));
}

bool UnixClientImpl::SendFds(const std::vector<int> &fds)
{
    if (!isRunning_ || !isConnected_)
        return false;

    if (fds.empty()) {
        LMNET_LOGW("No file descriptors to send");
        return true;
    }

    return SendUnixMessage(nullptr, fds);
}

bool UnixClientImpl::SendWithFds(std::shared_ptr<DataBuffer> data, const std::vector<int> &fds)
{
    if (!isRunning_ || !isConnected_)
        return false;

    if ((!data || data->Size() == 0) && fds.empty()) {
        LMNET_LOGW("No data and no file descriptors to send");
        return true;
    }

    return SendUnixMessage(data, fds);
}

bool UnixClientImpl::QueueStreamWrite(std::shared_ptr<DataBuffer> buffer)
{
    bool shouldSubmit = false;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.push_back(std::move(buffer));
        if (!writeInFlight_) {
            writeInFlight_ = true;
            shouldSubmit = true;
        }
    }

    if (!shouldSubmit) {
        return true;
    }

    if (SubmitNextWrite()) {
        return true;
    }

    if (auto listener = listener_.lock()) {
        listener->OnError(socket_, "Failed to submit write request");
    }
    HandleClose();
    return false;
}

bool UnixClientImpl::SubmitNextWrite()
{
    std::shared_ptr<DataBuffer> buffer;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (sendQueue_.empty()) {
            writeInFlight_ = false;
            return true;
        }
        if (!isRunning_ || !isConnected_ || socket_ == INVALID_SOCKET) {
            writeInFlight_ = false;
            return false;
        }
        buffer = sendQueue_.front();
    }

    auto self = shared_from_this();
    return IoUringManager::GetInstance().SubmitWriteRequest(socket_, buffer,
                                                            [self](int, int res) { self->HandleWriteComplete(res); });
}

void UnixClientImpl::HandleWriteComplete(int result)
{
    if (result < 0) {
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, std::string("Send error: ") + strerror(-result));
        }
        HandleClose();
        return;
    }

    bool shouldSubmitNext = false;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (sendQueue_.empty()) {
            writeInFlight_ = false;
            return;
        }

        auto &buffer = sendQueue_.front();
        if (result == 0) {
            writeInFlight_ = false;
        } else if (static_cast<size_t>(result) < buffer->Size()) {
            auto remaining = DataBuffer::PoolAlloc(buffer->Size() - static_cast<size_t>(result));
            remaining->Assign(buffer->Data() + result, buffer->Size() - static_cast<size_t>(result));
            buffer = remaining;
            shouldSubmitNext = true;
        } else {
            sendQueue_.pop_front();
            shouldSubmitNext = !sendQueue_.empty();
            writeInFlight_ = shouldSubmitNext;
        }
    }

    if (result == 0) {
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, "Send returned zero bytes");
        }
        HandleClose();
        return;
    }

    if (shouldSubmitNext && !SubmitNextWrite()) {
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, "Failed to submit write request");
        }
        HandleClose();
    }
}

bool UnixClientImpl::SendUnixMessage(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds)
{
    std::vector<int> duplicatedFds;
    if (!fds.empty()) {
        duplicatedFds = UnixSocketUtils::DuplicateFds(fds);
        if (duplicatedFds.empty()) {
            return false; // Failed to duplicate fds
        }
    }

    // Create a copy for cleanup callback before moving duplicatedFds
    std::vector<int> fdsForCleanup = duplicatedFds;
    auto cleanup = UnixSocketUtils::CreateCleanupCallback(std::move(fdsForCleanup), "Send Unix message");
    auto self = weak_from_this();
    bool submitted = IoUringManager::GetInstance().SubmitSendMsgRequest(
        socket_, buffer, duplicatedFds, [self, buffer, cleanup = std::move(cleanup)](int fd, int res) mutable {
            cleanup(fd, res);

            auto strong = self.lock();
            if (!strong) {
                return;
            }

            if (res < 0) {
                if (auto listener = strong->listener_.lock()) {
                    listener->OnError(fd, std::string("Send error: ") + strerror(-res));
                }
                strong->HandleClose();
                return;
            }

            if (res == 0) {
                if (auto listener = strong->listener_.lock()) {
                    listener->OnError(fd, "Send returned zero bytes");
                }
                strong->HandleClose();
                return;
            }

            if (buffer && static_cast<size_t>(res) < buffer->Size()) {
                auto remaining = DataBuffer::PoolAlloc(buffer->Size() - static_cast<size_t>(res));
                remaining->Assign(buffer->Data() + res, buffer->Size() - static_cast<size_t>(res));
                if (!strong->QueueStreamWrite(std::move(remaining))) {
                    if (auto listener = strong->listener_.lock()) {
                        listener->OnError(fd, "Failed to queue remaining Unix stream data");
                    }
                    strong->HandleClose();
                }
            }
        });
    if (!submitted) {
        UnixSocketUtils::CleanupFds(duplicatedFds);
    }
    return submitted;
}

void UnixClientImpl::Close()
{
    if (!isRunning_.exchange(false)) {
        return;
    }

    HandleClose();
}

void UnixClientImpl::HandleClose()
{
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.clear();
        writeInFlight_ = false;
    }

    if (isConnected_.exchange(false)) {
        if (auto listener = listener_.lock()) {
            listener->OnClose(socket_);
        }
    }
    if (socket_ != INVALID_SOCKET) {
        int socketToClose = socket_;
        if (!IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr)) {
            close(socketToClose);
        }
        socket_ = INVALID_SOCKET;
    }
    LMNET_LOGI("Unix client connection closed.");
}

} // namespace lmshao::lmnet
