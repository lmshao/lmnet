#include "unix_server_impl.h"

#include <sys/un.h>
#include <unistd.h>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "io_uring_session_impl.h"

namespace lmshao::lmnet {

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
    SubmitAccept();
    LMNET_LOGI("Unix server started on %s", socketPath_.c_str());
    return true;
}

bool UnixServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }

    // Close all client sessions
    for (auto const &[fd, session] : sessions_) {
        IoUringManager::GetInstance().SubmitCloseRequest(fd, nullptr);
    }
    sessions_.clear();

    // Close server socket
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }

    unlink(socketPath_.c_str());
    LMNET_LOGI("Unix server stopped.");
    return true;
}

void UnixServerImpl::SubmitAccept()
{
    if (!isRunning_)
        return;

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitAcceptRequest(
        socket_, [self](int fd, int client_fd, const sockaddr *, socklen_t *) { self->HandleAccept(client_fd); });
}

void UnixServerImpl::HandleAccept(int client_fd)
{
    if (client_fd >= 0) {
        auto session = std::make_shared<IoUringSessionImpl>(client_fd, "", 0);
        sessions_[client_fd] = session;

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
    if (isRunning_) {
        SubmitAccept();
    }
}

void UnixServerImpl::SubmitRead(int client_fd)
{
    if (!isRunning_)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitReadRequest(
        client_fd, buffer, [self, client_fd](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read) {
            self->HandleReceive(client_fd, buf, bytes_read);
        });
}

void UnixServerImpl::HandleReceive(int client_fd, std::shared_ptr<DataBuffer> buffer, int bytes_read)
{
    auto it = sessions_.find(client_fd);
    if (it == sessions_.end()) {
        return; // Session already closed
    }
    auto session = it->second;

    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            listener->OnReceive(session, buffer);
        }
        SubmitRead(client_fd); // Continue reading
    } else if (bytes_read == 0) {
        LMNET_LOGI("Client %d disconnected.", client_fd);
        HandleConnectionClose(client_fd);
    } else {
        if (isRunning_) {
            LMNET_LOGE("Read failed on client %d: %s", client_fd, strerror(-bytes_read));
            if (auto listener = listener_.lock()) {
                listener->OnError(session, strerror(-bytes_read));
            }
        }
        HandleConnectionClose(client_fd);
    }
}

void UnixServerImpl::HandleConnectionClose(int client_fd)
{
    auto it = sessions_.find(client_fd);
    if (it != sessions_.end()) {
        if (auto listener = listener_.lock()) {
            listener->OnClose(it->second);
        }
        sessions_.erase(it);
    }
    // The actual close is submitted to io_uring, no need to call close() here directly
    IoUringManager::GetInstance().SubmitCloseRequest(client_fd, nullptr);
}

} // namespace lmshao::lmnet
