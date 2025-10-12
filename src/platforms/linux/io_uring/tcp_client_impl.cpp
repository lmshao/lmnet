#include "tcp_client_impl.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"

namespace lmshao::lmnet {
using lmshao::lmcore::TaskHandler;

TcpClientImpl::TcpClientImpl(std::string remote_ip, uint16_t remote_port, std::string local_ip, uint16_t local_port)
    : remoteIp_(std::move(remote_ip)), remotePort_(remote_port), localIp_(std::move(local_ip)), localPort_(local_port),
      taskQueue_(std::make_unique<TaskQueue>("tcp_client_tq"))
{
    taskQueue_->Start();
}

TcpClientImpl::~TcpClientImpl()
{
    Close();
    taskQueue_->Stop();
}

bool TcpClientImpl::Init()
{
    ReInit();
    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        return false;
    }
    isRunning_ = true;
    return true;
}

void TcpClientImpl::ReInit()
{
    if (socket_ != INVALID_SOCKET) {
        close(socket_);
    }

    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return;
    }

    if (!localIp_.empty() || localPort_ != 0) {
        memset(&localAddr_, 0, sizeof(localAddr_));
        localAddr_.sin_family = AF_INET;
        localAddr_.sin_port = htons(localPort_);
        localAddr_.sin_addr.s_addr = localIp_.empty() ? htonl(INADDR_ANY) : inet_addr(localIp_.c_str());

        if (bind(socket_, (struct sockaddr *)&localAddr_, sizeof(localAddr_)) < 0) {
            LMNET_LOGE("Failed to bind socket: %s", strerror(errno));
            close(socket_);
            socket_ = INVALID_SOCKET;
        }
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    serverAddr_.sin_addr.s_addr = inet_addr(remoteIp_.c_str());
}

bool TcpClientImpl::Connect()
{
    if (!isRunning_ || socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Client is not initialized.");
        return false;
    }
    SubmitConnect();
    return true;
}

void TcpClientImpl::SubmitConnect()
{
    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitConnectRequest(socket_, serverAddr_,
                                                       [self](int fd, int res) { self->HandleConnect(res); });
}

void TcpClientImpl::HandleConnect(int result)
{
    if (result >= 0) {
        LMNET_LOGI("Successfully connected to %s:%d", remoteIp_.c_str(), remotePort_);
        isConnected_ = true;
        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock()] {
            if (listener) {
                // OnConnect is not part of the new IClientListener, just start receiving
            }
        });
        taskQueue_->EnqueueTask(task);
        SubmitRead();
    } else {
        LMNET_LOGE("Failed to connect: %s", strerror(-result));
        HandleClose(true, strerror(-result));
    }
}

void TcpClientImpl::SubmitRead()
{
    if (!isRunning_)
        return;
    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();
    IoUringManager::GetInstance().SubmitReadRequest(
        socket_, buffer,
        [self](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read) { self->HandleReceive(buf, bytes_read); });
}

void TcpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), buffer] {
            if (listener) {
                listener->OnReceive(GetSocketFd(), buffer);
            }
        });
        taskQueue_->EnqueueTask(task);
        SubmitRead();
    } else if (bytes_read == 0) {
        HandleClose(false, "Connection closed by peer");
    } else {
        HandleClose(true, std::string("Read error: ") + strerror(-bytes_read));
    }
}

bool TcpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool TcpClientImpl::Send(const void *data, size_t len)
{
    auto buffer = std::make_shared<DataBuffer>();
    buffer->Assign(data, len);
    return Send(buffer);
}

bool TcpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!isRunning_)
        return false;
    IoUringManager::GetInstance().SubmitWriteRequest(socket_, data, [self = shared_from_this()](int fd, int res) {
        if (res < 0) {
            LMNET_LOGE("Send failed: %s", strerror(-res));
        }
    });
    return true;
}

void TcpClientImpl::Close()
{
    if (!isRunning_.exchange(false)) {
        return;
    }
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(
            socket_, [self = shared_from_this()](int, int) { self->HandleClose(false, "Closed by client"); });
        socket_ = INVALID_SOCKET;
    }
}

void TcpClientImpl::HandleClose(bool is_error, const std::string &reason)
{
    if (isConnected_.exchange(false)) { // Ensure close logic runs only once
        LMNET_LOGI("Connection closed: %s", reason.c_str());
        auto task = std::make_shared<TaskHandler<void>>([this, listener = listener_.lock(), is_error, reason] {
            if (listener) {
                if (is_error) {
                    listener->OnError(GetSocketFd(), reason);
                }
                listener->OnClose(GetSocketFd());
            }
        });
        taskQueue_->EnqueueTask(task);
    }
}

} // namespace lmshao::lmnet
