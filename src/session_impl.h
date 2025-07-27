//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_SESSION_IMPL_H
#define NETWORK_SESSION_IMPL_H

#include "session.h"

class SessionImpl : public Session {
public:
    SessionImpl(int fd, std::string host, uint16_t port, std::shared_ptr<BaseServer> server) : server_(server)
    {
        this->host = host;
        this->port = port;
        this->fd = fd;
    }

    virtual ~SessionImpl() = default;

    bool Send(const void *data, size_t size) const override
    {
        auto server = server_.lock();
        if (server) {
            return server->Send(fd, host, port, data, size);
        }
        return false;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        if (!buffer) {
            return false;
        }
        return Send(buffer->Data(), buffer->Size());
    }

    bool Send(const std::string &str) const override { return Send(str.data(), str.size()); }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port << " (" << fd << ")";
        return ss.str();
    }

private:
    std::weak_ptr<BaseServer> server_;
};

#endif // NETWORK_SESSION_IMPL_H