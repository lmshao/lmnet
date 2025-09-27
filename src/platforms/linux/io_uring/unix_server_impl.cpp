#include "unix_server_impl.h"
#include "lmnet/session.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <map>

#include "lmcore/internal_logger.h"

namespace lmshao::lmnet {

namespace {
std::map<int, std::shared_ptr<Session>> g_sessions;
} // namespace

UnixServerImpl::UnixServerImpl(const std::string& path) : path_(path) {}

UnixServerImpl::~UnixServerImpl() {
    Stop();
}

bool UnixServerImpl::Init() {
    if (is_running_) {
        return true;
    }

    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ < 0) {
        LMNET_LOGE("Failed to create unix socket");
        return false;
    }

    unlink(path_.c_str());

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, path_.c_str(), sizeof(server_addr.sun_path) - 1);

    if (bind(socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LMNET_LOGE("Failed to bind unix socket");
        close(socket_);
        socket_ = -1;
        return false;
    }

    if (listen(socket_, SOMAXCONN) < 0) {
        LMNET_LOGE("Failed to listen on unix socket");
        close(socket_);
        socket_ = -1;
        return false;
    }

    IoUringManager::GetInstance().Init();
    is_running_ = true;
    StartAccept();

    return true;
}

void UnixServerImpl::Stop() {
    if (!is_running_) {
        return;
    }
    is_running_ = false;

    if (socket_ != -1) {
        close(socket_);
        socket_ = -1;
    }

    for (int client_fd : client_fds_) {
        close(client_fd);
    }
    client_fds_.clear();
    g_sessions.clear();
    unlink(path_.c_str());
}

void UnixServerImpl::StartAccept() {
    IoUringManager::GetInstance().SubmitAcceptRequest(
        socket_, [this](int client_fd, const sockaddr_in& client_addr) { OnAccept(client_fd, client_addr); });
}

void UnixServerImpl::OnAccept(int client_fd, const sockaddr_in& client_addr) {
    if (client_fd < 0) {
        LMNET_LOGE("Accept failed: %s", strerror(-client_fd));
        return;
    }

    LMNET_LOGI("Accepted new connection fd: %d", client_fd);
    client_fds_.push_back(client_fd);

    auto session = std::make_shared<Session>(client_fd);
    g_sessions[client_fd] = session;

    if (listener_) {
        listener_->OnAccept(session);
    }

    auto buffer = std::make_shared<lmcore::DataBuffer>(4096);
    IoUringManager::GetInstance().SubmitReadRequest(
        client_fd, buffer,
        [this, client_fd, buffer](int, std::shared_ptr<lmcore::DataBuffer>, int res) { OnRead(client_fd, buffer, res); });

    StartAccept();
}

void UnixServerImpl::OnRead(int client_fd, std::shared_ptr<lmcore::DataBuffer> data, int res) {
    auto session_it = g_sessions.find(client_fd);
    if (session_it == g_sessions.end()) {
        return;
    }
    auto session = session_it->second;

    if (res <= 0) {
        if (res < 0) {
            LMNET_LOGE("Read error: %s", strerror(-res));
            if (listener_) {
                listener_->OnError(session, strerror(-res));
            }
        } else {
            LMNET_LOGI("Connection closed by peer fd: %d", client_fd);
        }
        if (listener_) {
            listener_->OnClose(session);
        }
        close(client_fd);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd), client_fds_.end());
        g_sessions.erase(session_it);
        return;
    }

    data->SetSize(res);
    if (listener_) {
        listener_->OnReceive(session, data);
    }

    IoUringManager::GetInstance().SubmitReadRequest(
        client_fd, data,
        [this, client_fd, data](int, std::shared_ptr<lmcore::DataBuffer>, int read_res) { OnRead(client_fd, data, read_res); });
}

} // namespace lmshao::lmnet
