#include "unix_client_impl.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <vector>

#include "internal_logger.h"
#include "io_uring_manager.h"

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
    StartReceive();

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
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, strerror(-result));
        }
        // No need to call Close() here, as HandleClose will be called by the caller in case of error
    }
}

void UnixClientImpl::StartReceive()
{
    if (!isRunning_ || !isConnected_)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitReadRequest(
        socket_, buffer,
        [self](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read) { self->HandleReceive(buf, bytes_read); });
}

void UnixClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            listener->OnReceive(socket_, buffer);
        }
        StartReceive(); // Continue receiving
    } else if (bytes_read == 0) {
        LMNET_LOGI("Connection closed by peer.");
        HandleClose();
    } else {
        if (isRunning_) {
            LMNET_LOGE("Read failed: %s", strerror(-bytes_read));
            if (auto listener = listener_.lock()) {
                listener->OnError(socket_, strerror(-bytes_read));
            }
        }
        HandleClose();
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

    IoUringManager::GetInstance().SubmitWriteRequest(socket_, data, [](int, int res) {
        if (res < 0) {
            LMNET_LOGE("Write failed: %s", strerror(-res));
        }
    });
    return true;
}

bool UnixClientImpl::SendFds(const std::vector<int> &fds)
{
    (void)fds;
    LMNET_LOGE("SendFds is not supported on io_uring backend");
    return false;
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
    if (isConnected_.exchange(false)) {
        if (auto listener = listener_.lock()) {
            listener->OnClose(socket_);
        }
    }
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }
    LMNET_LOGI("Unix client connection closed.");
}

} // namespace lmshao::lmnet
