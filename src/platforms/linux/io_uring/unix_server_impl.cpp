#include "unix_server_impl.h"

#include <errno.h>
#include <sys/un.h>
#include <unistd.h>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "io_uring_session_impl.h"
#include "lmnet/unix_message.h"
#include "unix_socket_utils.h"

namespace lmshao::lmnet {

namespace {

bool ShouldResubmitAccept(int result)
{
    return result != -ENOMEM && result != -ESHUTDOWN && result != -ECANCELED;
}

bool IsFatalSubmissionError(int result)
{
    return result == -ENOMEM || result == -ESHUTDOWN || result == -ECANCELED;
}

} // namespace

UnixServerImpl::UnixServerImpl(const std::string &socketPath) : socketPath_(socketPath) {}

UnixServerImpl::~UnixServerImpl()
{
    Stop();
}

bool UnixServerImpl::Init()
{
    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    // Unlink the socket file to avoid bind errors if it already exists
    unlink(socketPath_.c_str());

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sun_family = AF_UNIX;
    strncpy(serverAddr_.sun_path, socketPath_.c_str(), sizeof(serverAddr_.sun_path) - 1);

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("Failed to bind socket to %s: %s", socketPath_.c_str(), strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    if (listen(socket_, SOMAXCONN) < 0) {
        LMNET_LOGE("Failed to listen on socket: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
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

bool UnixServerImpl::Start()
{
    if (!isRunning_)
        return false;
    if (!SubmitAccept()) {
        Stop();
        return false;
    }
    LMNET_LOGI("Unix server started on %s", socketPath_.c_str());
    return true;
}

bool UnixServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }

    std::vector<int> sessionFds;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        sessionFds.reserve(sessions_.size());
        for (const auto &[fd, session] : sessions_) {
            (void)session;
            sessionFds.push_back(fd);
        }
        sessions_.clear();
    }

    for (int fd : sessionFds) {
        if (!IoUringManager::GetInstance().SubmitCloseRequest(fd, nullptr)) {
            close(fd);
        }
    }

    // Close server socket
    if (socket_ != INVALID_SOCKET) {
        int socketToClose = socket_;
        if (!IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr)) {
            close(socketToClose);
        }
        socket_ = INVALID_SOCKET;
    }

    unlink(socketPath_.c_str());
    LMNET_LOGI("Unix server stopped.");
    return true;
}

bool UnixServerImpl::SubmitAccept()
{
    if (!isRunning_)
        return false;

    auto self = shared_from_this();
    return IoUringManager::GetInstance().SubmitAcceptRequest(
        socket_, [self](int fd, int client_fd, const sockaddr *, socklen_t *) { self->HandleAccept(client_fd); });
}

void UnixServerImpl::HandleAccept(int client_fd)
{
    if (client_fd >= 0) {
        auto self = shared_from_this();
        auto session = std::make_shared<IoUringSessionImpl>(
            client_fd, "", 0, IoUringSessionImpl::TransportKind::UNIX_STREAM,
            [self](socket_t fd, const std::string &) { self->HandleConnectionClose(fd); });
        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessions_[client_fd] = session;
        }

        if (auto listener = listener_.lock()) {
            listener->OnAccept(session);
        }

        SubmitRead(client_fd);
    } else {
        if (isRunning_) {
            LMNET_LOGE("Accept failed: %s", strerror(-client_fd));
        }
    }

    // Always try to accept the next connection
    if (isRunning_ && ShouldResubmitAccept(client_fd)) {
        SubmitAccept();
    }
}

void UnixServerImpl::SubmitRead(int client_fd)
{
    if (!isRunning_)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    // Use SubmitRecvMsgRequest to receive both data and file descriptors
    IoUringManager::GetInstance().SubmitRecvMsgRequest(
        client_fd, buffer,
        [self, client_fd](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read, std::vector<int> fds) {
            self->HandleReceiveWithFds(client_fd, buf, bytes_read, std::move(fds));
        });
}

void UnixServerImpl::HandleReceiveWithFds(int client_fd, std::shared_ptr<DataBuffer> buffer, int bytes_read,
                                          std::vector<int> fds)
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        auto it = sessions_.find(client_fd);
        if (it != sessions_.end()) {
            session = it->second;
        }
    }

    if (!session) {
        // Clean up any received file descriptors if session not found
        UnixSocketUtils::CleanupFds(fds);
        return; // Session already closed
    }

    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            UnixSocketUtils::ProcessServerMessage(listener, session, buffer, std::move(fds));
        } else {
            UnixSocketUtils::CleanupFds(fds);
        }
        SubmitRead(client_fd); // Continue reading
    } else if (bytes_read == 0) {
        // Clean up any received file descriptors if connection closed
        UnixSocketUtils::CleanupFds(fds);
        LMNET_LOGI("Client %d disconnected.", client_fd);
        HandleConnectionClose(client_fd);
    } else {
        // Clean up any received file descriptors on error
        UnixSocketUtils::CleanupFds(fds);

        // Handle different error types
        if (bytes_read == -EAGAIN || bytes_read == -EWOULDBLOCK) {
            // Non-blocking I/O would block - this is normal, retry
            SubmitRead(client_fd);
        } else if (isRunning_) {
            // Other errors - log and close
            LMNET_LOGE("Read failed on client %d: %s", client_fd, strerror(-bytes_read));
            if (auto listener = listener_.lock()) {
                listener->OnError(session, strerror(-bytes_read));
            }
            HandleConnectionClose(client_fd);
            if (IsFatalSubmissionError(bytes_read)) {
                return;
            }
        }
    }
}

void UnixServerImpl::HandleConnectionClose(int client_fd)
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        auto it = sessions_.find(client_fd);
        if (it != sessions_.end()) {
            session = it->second;
            sessions_.erase(it);
        }
    }

    if (session) {
        if (auto listener = listener_.lock()) {
            listener->OnClose(session);
        }
    }
    // The actual close is submitted to io_uring, no need to call close() here directly
    if (!IoUringManager::GetInstance().SubmitCloseRequest(client_fd, nullptr)) {
        close(client_fd);
    }
}

} // namespace lmshao::lmnet
