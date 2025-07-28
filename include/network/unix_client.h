//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_UNIX_CLIENT_H
#define NETWORK_UNIX_CLIENT_H

#include <sys/un.h>

#include <memory>
#include <string>

#include "data_buffer.h"
#include "iclient_listener.h"
#include "task_queue.h"

class EventHandler;
class UnixClientHandler;

class UnixClient : public std::enable_shared_from_this<UnixClient> {
    friend class UnixClientHandler;
    static constexpr int INVALID_SOCKET = -1;

public:
    static std::shared_ptr<UnixClient> Create(const std::string &socketPath)
    {
        return std::shared_ptr<UnixClient>(new UnixClient(socketPath));
    }

    ~UnixClient();

    bool Init();
    void SetListener(std::shared_ptr<IClientListener> listener) { listener_ = listener; }
    bool Connect();
    bool Send(const std::string &str);
    bool Send(const void *data, size_t len);
    bool Send(std::shared_ptr<DataBuffer> data);
    void Close();
    int GetSocketFd() const { return socket_; }

protected:
    explicit UnixClient(const std::string &socketPath);
    void HandleReceive(int fd);
    void HandleConnectionClose(int fd, bool isError, const std::string &reason);

private:
    std::string socketPath_;
    int socket_ = INVALID_SOCKET;
    struct sockaddr_un serverAddr_;

    std::weak_ptr<IClientListener> listener_;
    std::unique_ptr<TaskQueue> taskQueue_;
    std::shared_ptr<DataBuffer> readBuffer_;

    std::shared_ptr<UnixClientHandler> clientHandler_;
};

#endif // NETWORK_UNIX_CLIENT_H
