#include "udp_server_impl.h"

#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {

// UDP Session implementation for io_uring
class UdpSession : public Session {
public:
    UdpSession(socket_t fd, const std::string &remoteIp, uint16_t remotePort, UdpServerImpl *server)
        : fd_(fd), server_(server)
    {
        host = remoteIp;
        port = remotePort;
        fd = fd_;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        if (!server_) {
            return false;
        }
        return server_->Send(fd_, host, port, buffer);
    }

    bool Send(const std::string &str) const override
    {
        if (!server_) {
            return false;
        }
        return server_->Send(fd_, host, port, str);
    }

    bool Send(const void *data, size_t size) const override
    {
        if (!server_) {
            return false;
        }
        return server_->Send(fd_, host, port, data, size);
    }

    std::string ClientInfo() const override { return host + ":" + std::to_string(port); }

private:
    socket_t fd_;
    UdpServerImpl *server_;
};

UdpServerImpl::UdpServerImpl(std::string ip, uint16_t port) : ip_(std::move(ip)), port_(port) {}

UdpServerImpl::~UdpServerImpl()
{
    Stop();
}

bool UdpServerImpl::Init()
{
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
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

    memset(&serverAddr_, 0, sizeof(serverAddr_));
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port_);
    serverAddr_.sin_addr.s_addr = ip_.empty() ? htonl(INADDR_ANY) : inet_addr(ip_.c_str());

    if (bind(socket_, (struct sockaddr *)&serverAddr_, sizeof(serverAddr_)) < 0) {
        LMNET_LOGE("Failed to bind socket: %s", strerror(errno));
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

bool UdpServerImpl::Start()
{
    if (!isRunning_)
        return false;
    StartReceive();
    LMNET_LOGI("UDP server started on %s:%d", ip_.c_str(), port_);
    return true;
}

bool UdpServerImpl::Stop()
{
    if (!isRunning_.exchange(false)) {
        return true;
    }
    if (socket_ != INVALID_SOCKET) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = INVALID_SOCKET;
    }
    LMNET_LOGI("UDP server stopped.");
    return true;
}

void UdpServerImpl::StartReceive()
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

void UdpServerImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_in &from_addr)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            uint16_t client_port = ntohs(from_addr.sin_port);

            // Create session info like epoll implementation
            auto session = std::make_shared<UdpSession>(socket_, std::string(client_ip), client_port, this);

            // For UDP, call the listener with session
            listener->OnReceive(session, buffer);
        }
    } else if (bytes_read < 0) {
        if (isRunning_) {
            LMNET_LOGE("RecvFrom failed: %s", strerror(-bytes_read));
        }
    }

    // Always restart receiving unless the server is stopped.
    if (isRunning_) {
        StartReceive();
    }
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, const void *data, size_t len)
{
    auto buffer = std::make_shared<DataBuffer>();
    buffer->Assign(data, len);
    return Send(ip, port, buffer);
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, const std::string &str)
{
    return Send(ip, port, str.data(), str.size());
}

bool UdpServerImpl::Send(const std::string &ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    if (!isRunning_)
        return false;

    sockaddr_in client_addr{};
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(port);
    client_addr.sin_addr.s_addr = inet_addr(ip.c_str());

    IoUringManager::GetInstance().SubmitSendToRequest(socket_, data, client_addr, [](int, int res) {
        if (res < 0) {
            LMNET_LOGE("SendTo failed: %s", strerror(-res));
        }
    });
    return true;
}

// To satisfy BaseServer interface
bool UdpServerImpl::Send(socket_t, std::string ip, uint16_t port, const void *data, size_t len)
{
    return Send(ip, port, data, len);
}
bool UdpServerImpl::Send(socket_t, std::string ip, uint16_t port, const std::string &str)
{
    return Send(ip, port, str);
}
bool UdpServerImpl::Send(socket_t, std::string ip, uint16_t port, std::shared_ptr<DataBuffer> data)
{
    return Send(ip, port, data);
}

} // namespace lmshao::lmnet
