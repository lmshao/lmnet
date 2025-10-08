#ifndef LMSHAO_LMNET_LINUX_TCP_SESSION_IMPL_H
#define LMSHAO_LMNET_LINUX_TCP_SESSION_IMPL_H

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "lmnet/session.h"

namespace lmshao::lmnet {

class TcpSession : public Session {
public:
    TcpSession(socket_t fd, std::string host, uint16_t port)
    {
        this->fd = fd;
        this->host = std::move(host);
        this->port = port;
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        return IoUringManager::GetInstance().SubmitWriteRequest(fd, buffer, nullptr);
    }

    bool Send(const std::string &str) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(str.data(), str.size());
        return Send(buffer);
    }

    bool Send(const void *data, size_t size) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(data, size);
        return Send(buffer);
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << host << ":" << port;
        return ss.str();
    }
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_TCP_SESSION_IMPL_H
