#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"

namespace lmshao::lmnet {

UdpClientImpl::UdpClientImpl(std::string remoteIp, uint16_t remotePort, std::string localIp, uint16_t localPort)
    : remoteIp_(std::move(remoteIp)), remotePort_(remotePort), localIp_(std::move(localIp)), localPort_(localPort)
{
}

UdpClientImpl::~UdpClientImpl()
{
    Close();
}

bool UdpClientImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ == INVALID_SOCKET) {
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
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
            return false;
        }
    }

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(remotePort_);
    serverAddr_.sin_addr.s_addr = inet_addr(remoteIp_.c_str());

    if (!IoUringManager::GetInstance().Init()) {
        LMNET_LOGE("Failed to initialize IoUringManager");
        close(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }

    isRunning_ = true;
    StartReceive();
    return true;
}

bool UdpClientImpl::EnableBroadcast()
{
    int opt = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        LMNET_LOGE("Failed to set SO_BROADCAST: %s", strerror(errno));
        return false;
    }
    return true;
}

void UdpClientImpl::StartReceive()
{
    if (!isRunning_)
        return;

    auto buffer = DataBuffer::PoolAlloc();
    auto self = shared_from_this();

    IoUringManager::GetInstance().SubmitRecvFromRequest(
        socket_, buffer, [self](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read, const sockaddr_in &from) {
            self->HandleReceive(buf, bytes_read, from);
        });
}

void UdpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
            listener->OnReceiveFrom(socket_, std::string(ip_str), ntohs(from_addr.sin_port), buffer);
        }
    } else if (bytes_read < 0) {
        if (isRunning_) {
            LMNET_LOGE("RecvFrom failed: %s", strerror(-bytes_read));
        }
    }

    if (isRunning_) {
        StartReceive();
    }
}

bool UdpClientImpl::Send(const std::string &str)
{
    return Send(str.data(), str.size());
}

bool UdpClientImpl::Send(const void *data, size_t len)
{
    auto buffer = std::make_shared<DataBuffer>();
    buffer->Assign(data, len);
    return Send(buffer);
}

bool UdpClientImpl::Send(std::shared_ptr<DataBuffer> data)
{
    if (!isRunning_)
        return false;

    IoUringManager::GetInstance().SubmitSendToRequest(socket_, data, serverAddr_, [](int, int res) {
        if (res < 0) {
            LMNET_LOGE("SendTo failed: %s", strerror(-res));
        }
    });
    return true;
}

void UdpClientImpl::Close()
{
    if (!isRunning_.exchange(false)) {
        return;
    }
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }
}

void UdpClientImpl::HandleClose(bool is_error, const std::string &reason)
{
    // UDP is connectionless, so this is mostly for resource cleanup.
    // The listener's OnConnect(false) might not be semantically correct here.
}

} // namespace lmshao::lmnet
