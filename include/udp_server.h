//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_UDP_SERVER_H
#define NETWORK_UDP_SERVER_H

#include <cstdint>
#include <netinet/in.h>
#include <string>
#include <unordered_map>
#include "base_server.h"
#include "iserver_listener.h"
#include "session_impl.h"
#include "thread_pool.h"

class UdpServer final : public BaseServer, public std::enable_shared_from_this<UdpServer> {
    friend class EventProcessor;
    const int INVALID_SOCKET = -1;

public:
    template <typename... Args>
    static std::shared_ptr<UdpServer> Create(Args... args)
    {
        return std::shared_ptr<UdpServer>(new UdpServer(args...));
    }

    ~UdpServer();

    bool Init() override;
    void SetListener(std::shared_ptr<IServerListener> listener) override { listener_ = listener; }
    bool Start() override;
    bool Stop() override;
    bool Send(int fd, std::string host, uint16_t port, std::shared_ptr<DataBuffer> buffer) override;

    int GetSocketFd() const { return socket_; }

    static uint16_t GetIdlePort();
    static uint16_t GetIdlePortPair();

protected:
    UdpServer(std::string listenIp, uint16_t listenPort) : localIp_(listenIp), localPort_(listenPort) {}
    explicit UdpServer(uint16_t listenPort) : localPort_(listenPort) {}

    void HandleReceive(int fd);

private:
    std::string localIp_ = "0.0.0.0";
    uint16_t localPort_;

    int socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IServerListener> listener_;
    std::unordered_map<int, std::shared_ptr<SessionImpl>> sessions_;
    std::unique_ptr<ThreadPool> callbackThreads_;
};

#endif // NETWORK_UDP_SERVER_H