/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "platforms/linux/io_uring/tcp_server_impl.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "session_impl.h"

namespace lmshao::lmnet {

TcpServerImpl::TcpServerImpl(std::string local_ip, uint16_t local_port)
    : local_ip_(std::move(local_ip)), local_port_(local_port)
{
    task_queue_ = std::make_unique<TaskQueue>("TcpServerCb");
}

TcpServerImpl::~TcpServerImpl()
{
    Stop();
    if (task_queue_) {
        task_queue_->Stop();
    }
}

bool TcpServerImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket error: %s", strerror(errno));
        return false;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(local_port_);
    if (local_ip_.empty()) {
        local_ip_ = "0.0.0.0";
    }
    inet_aton(local_ip_.c_str(), &server_addr.sin_addr);

    int optval = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        LMNET_LOGE("setsockopt SO_REUSEADDR error: %s", strerror(errno));
        return false;
    }

    if (bind(socket_, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        LMNET_LOGE("bind error: %s", strerror(errno));
        return false;
    }

    return true;
}

bool TcpServerImpl::Start()
{
    if (listen(socket_, SOMAXCONN) < 0) {
        LMNET_LOGE("listen error: %s", strerror(errno));
        return false;
    }

    auto &manager = IoUringManager::GetInstance();
    if (!manager.Init()) {
        LMNET_LOGE("IoUringManager init failed");
        return false;
    }

    is_running_ = true;
    task_queue_->Start();
    LMNET_LOGD("Server started, listening on %s:%d", local_ip_.c_str(), local_port_);

    SubmitAccept();
    return true;
}

bool TcpServerImpl::Stop()
{
    if (!is_running_.exchange(false)) {
        return true;
    }

    if (socket_ != INVALID_SOCKET) {
        auto &manager = IoUringManager::GetInstance();
        manager.SubmitCloseRequest(socket_, [this, &manager](int, int) {
            std::lock_guard<std::mutex> lock(session_mutex_);
            for (auto &pair : sessions_) {
                manager.SubmitCloseRequest(pair.first, [](int, int) {});
            }
            sessions_.clear();
        });
        socket_ = INVALID_SOCKET;
    }

    IoUringManager::GetInstance().Stop();

    if (task_queue_) {
        task_queue_->Stop();
    }

    LMNET_LOGD("Server stopped");
    return true;
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, const std::string &str)
{
    return Send(fd, "", 0, str.data(), str.size());
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, const void *data, size_t len)
{
    auto buffer = std::make_shared<DataBuffer>(len);
    buffer->Append(static_cast<const char *>(data), len);
    return Send(fd, "", 0, buffer);
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, std::shared_ptr<DataBuffer> data)
{
    if (!is_running_) {
        return false;
    }

    auto &manager = IoUringManager::GetInstance();
    return manager.SubmitWriteRequest(fd, data, [this, fd](int, int res) {
        if (res < 0) {
            LMNET_LOGE("Write error on fd %d: %s", fd, strerror(-res));
            HandleConnectionClose(fd, "Write error");
        }
    });
}

void TcpServerImpl::Disconnect(socket_t fd)
{
    if (!is_running_) {
        return;
    }

    HandleConnectionClose(fd, "Client disconnected");
}

void TcpServerImpl::SubmitAccept()
{
    if (!is_running_) {
        return;
    }

    auto &manager = IoUringManager::GetInstance();
    auto self = shared_from_this();
    manager.SubmitAcceptRequest(
        socket_, [this, self](int res, const sockaddr_in &client_addr) { HandleAccept(res, client_addr); });
}

void TcpServerImpl::HandleAccept(int res, const sockaddr_in &client_addr)
{
    if (!is_running_) {
        return;
    }

    if (res < 0) {
        LMNET_LOGE("Accept error: %s", strerror(-res));
        SubmitAccept();
        return;
    }

    int client_fd = res;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client_addr.sin_port);

    auto session = std::make_shared<SessionImpl>(client_fd, std::string(client_ip), client_port, shared_from_this());
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        sessions_[client_fd] = session;
    }

    if (auto listener = listener_.lock()) {
        auto task = std::make_shared<TaskHandler<void>>([listener, session]() { listener->OnAccept(session); });
        task_queue_->EnqueueTask(task);
    }

    SubmitRead(session);
    SubmitAccept();
}

void TcpServerImpl::SubmitRead(std::shared_ptr<Session> session)
{
    if (!is_running_) {
        return;
    }

    auto buffer = std::make_shared<DataBuffer>(4096);
    auto &manager = IoUringManager::GetInstance();
    auto self = shared_from_this();
    manager.SubmitReadRequest(session->fd, buffer,
                              [this, self, session](int, std::shared_ptr<DataBuffer> buf, int bytes_read) {
                                  HandleReceive(session, buf, bytes_read);
                              });
}

void TcpServerImpl::HandleReceive(std::shared_ptr<Session> session, std::shared_ptr<DataBuffer> buffer, int bytes_read)
{
    if (!is_running_) {
        return;
    }

    if (bytes_read <= 0) {
        if (bytes_read < 0) {
            LMNET_LOGE("Read error on fd %d: %s", session->fd, strerror(-bytes_read));
        }
        HandleConnectionClose(session->fd, "Client closed connection");
        return;
    }

    buffer->SetSize(bytes_read);
    if (auto listener = listener_.lock()) {
        auto task = std::make_shared<TaskHandler<void>>(
            [listener, session, buffer]() { listener->OnReceive(session, buffer); });
        task_queue_->EnqueueTask(task);
    }

    SubmitRead(session);
}

void TcpServerImpl::HandleConnectionClose(int client_fd, const std::string &reason)
{
    if (!is_running_) {
        return;
    }

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(session_mutex_);
        auto it = sessions_.find(client_fd);
        if (it != sessions_.end()) {
            session = it->second;
            sessions_.erase(it);
        }
    }

    if (session) {
        if (auto listener = listener_.lock()) {
            auto task =
                std::make_shared<TaskHandler<void>>([listener, session]() { listener->OnClose(session); });
            task_queue_->EnqueueTask(task);
        }

        auto &manager = IoUringManager::GetInstance();
        manager.SubmitCloseRequest(client_fd, [](int, int) {});
    }
}

} // namespace lmshao::lmnet