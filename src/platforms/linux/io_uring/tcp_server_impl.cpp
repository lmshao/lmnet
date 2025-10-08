/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "tcp_server_impl.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "tcp_session_impl.h"

namespace lmshao::lmnet {

TcpServerImpl::TcpServerImpl(std::string local_ip, uint16_t local_port)
    : localIp_(std::move(local_ip)), localPort_(local_port), taskQueue_(std::make_unique<TaskQueue>("tcp_server_tq"))
{
    taskQueue_->Start();
}

TcpServerImpl::~TcpServerImpl()
{
    if (isRunning_) {
        Stop();
    }
    taskQueue_->Stop();
}

bool TcpServerImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    int opt = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LMNET_LOGE("Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(localPort_);
    server_addr.sin_addr.s_addr = inet_addr(localIp_.c_str());

    if (bind(socket_, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        LMNET_LOGE("Failed to bind socket: %s", strerror(errno));
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
    LMNET_LOGI("TCP server initialized on %s:%d", localIp_.c_str(), localPort_);
    return true;
}

bool TcpServerImpl::Start()
{
    if (!isRunning_) {
        LMNET_LOGW("Server is not initialized or has been stopped.");
        return false;
    }
    SubmitAccept();
    LMNET_LOGI("TCP server started.");
    return true;
}

bool TcpServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }

    LMNET_LOGI("Stopping TCP server...");

    // Close listening socket first to prevent new connections
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }

    // Disconnect all active sessions
    std::unique_lock<std::mutex> lock(sessionMutex_);
    for (auto const &[fd, session] : sessions_) {
        Disconnect(fd);
    }
    sessions_.clear();
    lock.unlock();

    LMNET_LOGI("TCP server stopped.");
    return true;
}

void TcpServerImpl::SubmitAccept()
{
    if (!isRunning_)
        return;

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitAcceptRequest(
        socket_, [self](int fd, int client_fd, const sockaddr *addr, socklen_t *addrlen) {
            if (client_fd >= 0) {
                const sockaddr_in *client_addr = reinterpret_cast<const sockaddr_in *>(addr);
                self->HandleAccept(client_fd, *client_addr);
            } else {
                LMNET_LOGE("Accept failed with error: %s", strerror(-client_fd));
            }

            // Resubmit for next connection
            if (self->isRunning_) {
                self->SubmitAccept();
            }
        });
}

void TcpServerImpl::HandleAccept(int res, const sockaddr_in &client_addr)
{
    if (res >= 0) {
        int client_fd = res;
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        uint16_t client_port = ntohs(client_addr.sin_port);

        auto session = std::make_shared<TcpSession>(client_fd, std::string(client_ip), client_port);

        {
            std::lock_guard<std::mutex> lock(sessionMutex_);
            sessions_[client_fd] = session;
        }

        LMNET_LOGI("Accepted new connection from %s:%d on fd %d", client_ip, client_port, client_fd);

        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session] {
            if (listener) {
                listener->OnAccept(session);
            }
        });
        taskQueue_->EnqueueTask(task);

        SubmitRead(session);
        SubmitAccept(); // Continue accepting new connections
    } else {
        if (isRunning_) {
            LMNET_LOGE("Accept failed: %s", strerror(-res));
            if (errno == EMFILE || errno == ENFILE) {
                LMNET_LOGW("Too many open files, retrying accept later.");
                sleep(1);
                SubmitAccept();
            }
        }
    }
}

void TcpServerImpl::SubmitRead(std::shared_ptr<Session> session)
{
    if (!isRunning_ || !session)
        return;

    auto buffer = lmcore::DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitReadRequest(
        session->fd, buffer, [self, session](int fd, std::shared_ptr<lmcore::DataBuffer> buf, int bytes_read) {
            self->HandleReceive(session, buf, bytes_read);
        });
}

void TcpServerImpl::HandleReceive(std::shared_ptr<Session> session, std::shared_ptr<lmcore::DataBuffer> buffer,
                                  int bytes_read)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);

        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session, buffer] {
            if (listener) {
                listener->OnReceive(session, buffer);
            }
        });
        taskQueue_->EnqueueTask(task);

        SubmitRead(session); // Continue reading from the client
    } else if (bytes_read == 0) {
        HandleConnectionClose(session->fd, "Connection closed by peer");
    } else {
        HandleConnectionClose(session->fd, std::string("Read error: ") + strerror(-bytes_read));
    }
}

void TcpServerImpl::HandleConnectionClose(int client_fd, const std::string &reason)
{
    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.count(client_fd)) {
            session = sessions_[client_fd];
            sessions_.erase(client_fd);
        }
    }

    if (session) {
        LMNET_LOGI("Connection closed for fd %d: %s", client_fd, reason.c_str());
        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), session] {
            if (listener) {
                listener->OnClose(session);
            }
        });
        taskQueue_->EnqueueTask(task);
    }
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, const void *data, size_t size)
{
    auto buffer = std::make_shared<lmcore::DataBuffer>();
    buffer->Assign(data, size);
    return Send(fd, "", 0, buffer);
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, std::shared_ptr<DataBuffer> buffer)
{
    if (!isRunning_)
        return false;

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.find(fd) == sessions_.end()) {
            LMNET_LOGW("Attempted to send to an unknown session (fd: %d)", fd);
            return false;
        }
        session = sessions_[fd];
    }

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitWriteRequest(fd, buffer, [self, session, buffer](int res_fd, int res) {
        auto task = std::make_shared<TaskHandler<void>>([listener = self->listener_.lock(), session, res] {
            if (listener) {
                if (res < 0) {
                    listener->OnError(session, strerror(-res));
                }
            }
        });
        self->taskQueue_->EnqueueTask(task);
    });

    return true;
}

bool TcpServerImpl::Send(socket_t fd, std::string, uint16_t, const std::string &str)
{
    return Send(fd, "", 0, str.c_str(), str.length());
}

void TcpServerImpl::Disconnect(socket_t fd)
{
    if (!isRunning_)
        return;

    std::shared_ptr<Session> session;
    {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        if (sessions_.find(fd) == sessions_.end()) {
            LMNET_LOGW("Attempted to disconnect an unknown session (fd: %d)", fd);
            return;
        }
        session = sessions_[fd];
    }

    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitCloseRequest(
        fd, [self, fd](int, int) { self->HandleConnectionClose(fd, "Disconnected by server"); });
}

} // namespace lmshao::lmnet
