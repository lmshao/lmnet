#include "platforms/linux/io_uring/tcp_client_impl.h"

#include <arpa/inet.h>
#include <lmcore/task_queue.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "session_impl.h"

namespace lmshao::lmnet {

TcpClientImpl::TcpClientImpl(std::string remote_ip, uint16_t remote_port, std::string local_ip, uint16_t local_port)
    : remoteIp_(std::move(remote_ip)), remotePort_(remote_port), localIp_(std::move(local_ip)), localPort_(local_port)
{
    task_queue_ = std::make_unique<TaskQueue>("TcpClientCb");
}

TcpClientImpl::~TcpClientImpl()
{
    if (task_queue_) {
        task_queue_->Stop();
    }
    Close();
}

bool TcpClientImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Socket error: %s", strerror(errno));
        return false;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(localPort_);
        if (localIp_.empty()) {
            localIp_ = "0.0.0.0";
        }
        inet_aton(localIp_.c_str(), &local_addr.sin_addr);

        int optval = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            LMNET_LOGE("setsockopt SO_REUSEADDR error: %s", strerror(errno));
            return false;
        }

        if (bind(socket_, (struct sockaddr *)&local_addr, sizeof(local_addr)) != 0) {
            LMNET_LOGE("bind error: %s", strerror(errno));
            return false;
        }
    }

    return true;
}

void TcpClientImpl::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
        socket_ = INVALID_SOCKET;
    }
    Init();
}

bool TcpClientImpl::Connect()
{
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("socket not initialized");
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(remotePort_);
    if (remoteIp_.empty()) {
        remoteIp_ = "127.0.0.1";
    }
    inet_aton(remoteIp_.c_str(), &server_addr.sin_addr);

    auto &manager = IoUringManager::GetInstance();
    auto self = shared_from_this();

    manager.SubmitConnectRequest(socket_, server_addr, [self, this](int fd, int res) {
        if (res < 0) {
            LMNET_LOGE("Connect failed: %s", strerror(-res));
            if (!listener_.expired()) {
                auto listener = listener_.lock();
                if (listener) {
                    task_queue_->EnqueueTask(std::make_shared<TaskHandler<void>>(
                        [listener, fd, err = strerror(-res)]() { listener->OnError(fd, err); }));
                }
            }
            Close();
            return;
        }

        LMNET_LOGD("Connect success");
        is_running_ = true;
        task_queue_->Start();

        // No OnConnect in IClientListener, user can assume connection is ready after Connect returns and no OnError
        // is fired.

        StartAsyncRead();
    });

    return true;
}

void TcpClientImpl::StartAsyncRead()
{
    auto &manager = IoUringManager::GetInstance();
    auto self = shared_from_this();
    auto buffer = DataBuffer::PoolAlloc(4096);

    manager.SubmitReadRequest(socket_, buffer, [self, this](int fd, std::shared_ptr<DataBuffer> buf, int res) {
        if (res <= 0) {
            if (res < 0) {
                LMNET_LOGE("Recv failed: %s", strerror(-res));
                if (!listener_.expired()) {
                    auto listener = listener_.lock();
                    if (listener) {
                        task_queue_->EnqueueTask(std::make_shared<TaskHandler<void>>(
                            [listener, fd, err = strerror(-res)]() { listener->OnError(fd, err); }));
                    }
                }
            } else {
                LMNET_LOGD("Connection closed by peer");
                if (!listener_.expired()) {
                    auto listener = listener_.lock();
                    if (listener) {
                        task_queue_->EnqueueTask(
                            std::make_shared<TaskHandler<void>>([listener, fd]() { listener->OnClose(fd); }));
                    }
                }
            }
            Close();
            return;
        }

        if (!listener_.expired()) {
            auto listener = listener_.lock();
            if (listener) {
                buf->SetSize(res);
                task_queue_->EnqueueTask(
                    std::make_shared<TaskHandler<void>>([listener, fd, buf]() { listener->OnReceive(fd, buf); }));
            }
        }

        StartAsyncRead();
    });
}

bool TcpClientImpl::Send(const std::string &str)
{
    if (str.empty()) {
        LMNET_LOGE("Invalid string data");
        return false;
    }
    return Send(str.data(), str.size());
}

bool TcpClientImpl::Send(const void *data, size_t len)
{
    if (!data || len == 0) {
        LMNET_LOGE("Invalid data");
        return false;
    }

    auto buf = DataBuffer::PoolAlloc(len);
    buf->Assign(data, len);
    return Send(buf);
}

bool TcpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!data || data->Size() == 0) {
        LMNET_LOGE("Invalid data buffer");
        return false;
    }

    if (socket_ == INVALID_SOCKET || !is_running_) {
        LMNET_LOGE("socket not connected or running");
        return false;
    }

    auto &manager = IoUringManager::GetInstance();
    auto self = shared_from_this();

    manager.SubmitWriteRequest(socket_, data, [self, this](int fd, int res) {
        if (res < 0) {
            LMNET_LOGE("Send failed: %s", strerror(-res));
            if (!listener_.expired()) {
                auto listener = listener_.lock();
                if (listener) {
                    task_queue_->EnqueueTask(std::make_shared<TaskHandler<void>>(
                        [listener, fd, err = strerror(-res)]() { listener->OnError(fd, err); }));
                }
            }
            Close();
        }
    });

    return true;
}

void TcpClientImpl::Close()
{
    if (socket_ != INVALID_SOCKET) {
        is_running_ = false;
        auto &manager = IoUringManager::GetInstance();
        manager.SubmitCloseRequest(socket_, [](int, int) {});
        socket_ = INVALID_SOCKET;
    }
}

} // namespace lmshao::lmnet
