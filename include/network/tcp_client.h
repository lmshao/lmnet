//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_TCP_CLIENT_H
#define NETWORK_TCP_CLIENT_H

#include <netinet/in.h>

#include <cstdint>
#include <string>

#include "data_buffer.h"
#include "iclient_listener.h"
#include "task_queue.h"

class TcpClient final {
    friend class EventProcessor;
    const int INVALID_SOCKET = -1;

public:
    template <typename... Args>
    static std::shared_ptr<TcpClient> Create(Args &&...args)
    {
        return std::shared_ptr<TcpClient>(new TcpClient(std::forward<Args>(args)...));
    }

    ~TcpClient();

    bool Init();
    void SetListener(std::shared_ptr<IClientListener> listener) { listener_ = listener; }
    bool Connect();

    bool Send(const std::string &str);
    bool Send(const char *data, size_t len);
    bool Send(std::shared_ptr<DataBuffer> data);

    void Close();

    int GetSocketFd() const { return socket_; }

protected:
    TcpClient(std::string remoteIp, uint16_t remotePort, std::string localIp = "", uint16_t localPort = 0);

    void HandleReceive(int fd);
    void ReInit();

private:
    std::string remoteIp_;
    uint16_t remotePort_;

    std::string localIp_;
    uint16_t localPort_;

    int socket_ = INVALID_SOCKET;
    struct sockaddr_in serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::unique_ptr<DataBuffer> readBuffer_;
};

#endif // NETWORK_TCP_CLIENT_H