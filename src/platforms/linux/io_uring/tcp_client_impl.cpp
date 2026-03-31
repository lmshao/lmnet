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
    return SubmitConnect();
}

bool TcpClientImpl::SubmitConnect()
{
    auto self = shared_from_this();
    return IoUringManager::GetInstance().SubmitConnectRequest(socket_, serverAddr_,
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
        ReInit();
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
        HandleClose(socket_, false, "Connection closed by peer");
        Close();
    } else {
        HandleClose(socket_, true, std::string("Read error: ") + strerror(-bytes_read));
        Close();
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
    if (!data || data->Size() == 0) {
        LMNET_LOGE("Invalid data buffer");
        return false;
    }

    if (!isRunning_ || socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Client is not initialized.");
        return false;
    }

    bool shouldSubmit = false;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.push_back(std::move(data));
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

    HandleClose(socket_, true, "Failed to submit write request");
    Close();
    return false;
}

bool TcpClientImpl::SubmitNextWrite()
{
    std::shared_ptr<DataBuffer> buffer;
    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        if (sendQueue_.empty()) {
            writeInFlight_ = false;
            return true;
        }
        if (!isRunning_ || socket_ == INVALID_SOCKET) {
            writeInFlight_ = false;
            return false;
        }
        buffer = sendQueue_.front();
    }

    auto self = shared_from_this();
    return IoUringManager::GetInstance().SubmitWriteRequest(socket_, buffer,
                                                            [self](int, int res) { self->HandleWriteComplete(res); });
}

void TcpClientImpl::HandleWriteComplete(int result)
{
    if (result < 0) {
        LMNET_LOGE("Send failed: %s", strerror(-result));
        HandleClose(socket_, true, std::string("Send error: ") + strerror(-result));
        Close();
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
        HandleClose(socket_, true, "Send returned zero bytes");
        Close();
        return;
    }

    if (shouldSubmitNext && !SubmitNextWrite()) {
        HandleClose(socket_, true, "Failed to submit write request");
        Close();
    }
}

void TcpClientImpl::Close()
{
    bool wasRunning = isRunning_.exchange(false);
    bool wasConnected = isConnected_.exchange(false);

    if (!wasRunning) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(sendMutex_);
        sendQueue_.clear();
        writeInFlight_ = false;
    }

    if (socket_ != INVALID_SOCKET) {
        int socketToClose = socket_;
        bool submitted = IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
        if (!submitted) {
            close(socketToClose);
        }

        if (wasConnected) {
            NotifyClose(socketToClose, false, "Closed by client");
        }
    }
}

void TcpClientImpl::HandleClose(socket_t fd, bool is_error, const std::string &reason)
{
    if (isConnected_.exchange(false)) { // Ensure close logic runs only once
        NotifyClose(fd, is_error, reason);
    }
}

void TcpClientImpl::NotifyClose(socket_t fd, bool is_error, const std::string &reason)
{
    LMNET_LOGI("Connection closed: %s", reason.c_str());
    auto task = std::make_shared<TaskHandler<void>>([listener = listener_.lock(), is_error, reason, fd] {
        if (listener) {
            if (is_error) {
                listener->OnError(fd, reason);
            }
            listener->OnClose(fd);
        }
    });
    taskQueue_->EnqueueTask(task);
}

} // namespace lmshao::lmnet
