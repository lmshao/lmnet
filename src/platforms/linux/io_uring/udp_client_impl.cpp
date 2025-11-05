#include "udp_client_impl.h"

#include <arpa/inet.h>
#include <netdb.h>
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
    // Resolve remote server (single-stack)
    struct addrinfo hints {};
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_NUMERICHOST; // only numeric IPs

    char port_str[16] = {0};
    snprintf(port_str, sizeof(port_str), "%u", static_cast<unsigned>(remotePort_));

    struct addrinfo *res = nullptr;
    int gai = getaddrinfo(remoteIp_.c_str(), port_str, &hints, &res);
    if (gai != 0) {
        LMNET_LOGE("getaddrinfo(remote) failed: %s", gai_strerror(gai));
        return false;
    }

    // Pick first candidate only
    const struct addrinfo *picked = nullptr;
    for (struct addrinfo *ai = res; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET || ai->ai_family == AF_INET6) {
            picked = ai;
            break;
        }
    }
    if (!picked) {
        freeaddrinfo(res);
        LMNET_LOGE("No suitable address found for remote %s:%u", remoteIp_.c_str(), remotePort_);
        return false;
    }

    socket_ = socket(picked->ai_family, picked->ai_socktype, picked->ai_protocol);
    if (socket_ == INVALID_SOCKET) {
        freeaddrinfo(res);
        LMNET_LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }

    // Optional local bind
    if (!localIp_.empty() || localPort_ != 0) {
        struct addrinfo lhints {};
        memset(&lhints, 0, sizeof(lhints));
        lhints.ai_family = picked->ai_family; // bind with same family
        lhints.ai_socktype = SOCK_DGRAM;
        lhints.ai_flags = AI_PASSIVE;

        char lport_str[16] = {0};
        snprintf(lport_str, sizeof(lport_str), "%u", static_cast<unsigned>(localPort_));
        struct addrinfo *lres = nullptr;
        int lgai = getaddrinfo(localIp_.empty() ? nullptr : localIp_.c_str(), lport_str, &lhints, &lres);
        if (lgai != 0) {
            LMNET_LOGE("getaddrinfo(local) failed: %s", gai_strerror(lgai));
            close(socket_);
            socket_ = INVALID_SOCKET;
            freeaddrinfo(res);
            return false;
        }
        if (bind(socket_, lres->ai_addr, lres->ai_addrlen) < 0) {
            LMNET_LOGE("Failed to bind local addr: %s", strerror(errno));
            close(socket_);
            socket_ = INVALID_SOCKET;
            freeaddrinfo(lres);
            freeaddrinfo(res);
            return false;
        }
        memcpy(&localAddr_, lres->ai_addr, lres->ai_addrlen);
        localAddrLen_ = lres->ai_addrlen;
        freeaddrinfo(lres);
    }

    memcpy(&serverAddr_, picked->ai_addr, picked->ai_addrlen);
    serverAddrLen_ = picked->ai_addrlen;
    freeaddrinfo(res);

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
        socket_, buffer,
        [self](int fd, std::shared_ptr<DataBuffer> buf, int bytes_read, const sockaddr_storage &from, socklen_t len) {
            self->HandleReceive(buf, bytes_read, from, len);
        });
}

void UdpClientImpl::HandleReceive(std::shared_ptr<DataBuffer> buffer, int bytes_read, const sockaddr_storage &from_addr,
                                  socklen_t addrlen)
{
    if (bytes_read > 0) {
        buffer->SetSize(bytes_read);
        if (auto listener = listener_.lock()) {
            char ip_str[INET6_ADDRSTRLEN] = {0};
            if (from_addr.ss_family == AF_INET) {
                const sockaddr_in *in = reinterpret_cast<const sockaddr_in *>(&from_addr);
                inet_ntop(AF_INET, &in->sin_addr, ip_str, sizeof(ip_str));
            } else if (from_addr.ss_family == AF_INET6) {
                const sockaddr_in6 *in6 = reinterpret_cast<const sockaddr_in6 *>(&from_addr);
                inet_ntop(AF_INET6, &in6->sin6_addr, ip_str, sizeof(ip_str));
            }
            listener->OnReceive(socket_, buffer);
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

    IoUringManager::GetInstance().SubmitSendToRequest(socket_, data, reinterpret_cast<const sockaddr *>(&serverAddr_),
                                                      serverAddrLen_, [](int, int res) {
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
