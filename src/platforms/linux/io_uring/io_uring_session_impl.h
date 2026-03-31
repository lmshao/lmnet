/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025-2026 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H
#define LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H

#include <arpa/inet.h>
#include <netinet/in.h>

#include <cstring>
#include <deque>
#include <functional>
#include <mutex>

#include "internal_logger.h"
#include "io_uring_manager.h"
#include "lmnet/session.h"
#include "unix_socket_utils.h"

namespace lmshao::lmnet {

class IoUringSessionImpl : public Session, public std::enable_shared_from_this<IoUringSessionImpl> {
public:
    enum class TransportKind {
        TCP_STREAM,
        UDP_DATAGRAM,
        UNIX_STREAM,
    };

    IoUringSessionImpl(socket_t fd, std::string host, uint16_t port,
                       TransportKind transportKind = TransportKind::TCP_STREAM,
                       std::function<void(socket_t, const std::string &)> errorHandler = {})
        : transportKind_(transportKind), errorHandler_(std::move(errorHandler))
    {
        SetPeer(std::move(host), port);
        SetNativeHandle(fd);
    }

    IoUringSessionImpl(socket_t server_socket, std::string remote_host, uint16_t remote_port, bool is_udp)
        : transportKind_(is_udp ? TransportKind::UDP_DATAGRAM : TransportKind::TCP_STREAM)
    {
        SetPeer(std::move(remote_host), remote_port);
        SetNativeHandle(server_socket);

        if (transportKind_ == TransportKind::UDP_DATAGRAM) {
            memset(&client_addr_, 0, sizeof(client_addr_));
            client_addr_.sin_family = AF_INET;
            client_addr_.sin_port = htons(remote_port);
            client_addr_.sin_addr.s_addr = inet_addr(Host().c_str());
        }
    }

    ~IoUringSessionImpl() override = default;

    bool Send(const void *data, size_t size) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(data, size);
        return Send(buffer);
    }

    bool Send(std::shared_ptr<DataBuffer> buffer) const override
    {
        if (!buffer || buffer->Size() == 0) {
            LMNET_LOGW("No data to send");
            return false;
        }

        if (transportKind_ == TransportKind::UDP_DATAGRAM) {
            return IoUringManager::GetInstance().SubmitSendToRequest(
                NativeHandle(), buffer, client_addr_, [](int, int res) {
                    if (res < 0) {
                        LMNET_LOGE("UDP Send failed: %s", strerror(-res));
                    }
                });
        }

        return QueueStreamWrite(std::move(buffer));
    }

    bool Send(const std::string &str) const override
    {
        auto buffer = std::make_shared<DataBuffer>();
        buffer->Assign(str.data(), str.size());
        return Send(buffer);
    }

    bool SendFds(const std::vector<int> &fds) const override
    {
        if (transportKind_ != TransportKind::UNIX_STREAM) {
            LMNET_LOGE("SendFds is only supported on Unix stream sockets");
            return false;
        }

        return SendUnixMessage(nullptr, fds);
    }

    bool SendWithFds(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds) const override
    {
        if (transportKind_ != TransportKind::UNIX_STREAM) {
            LMNET_LOGE("SendWithFds is only supported on Unix stream sockets");
            return false;
        }

        if ((!buffer || buffer->Size() == 0) && fds.empty()) {
            LMNET_LOGW("No data and no file descriptors to send");
            return true;
        }

        return SendUnixMessage(buffer, fds);
    }

    std::string ClientInfo() const override
    {
        std::stringstream ss;
        ss << Host() << ":" << Port() << " (" << NativeHandle() << ")";
        return ss.str();
    }

private:
    bool QueueStreamWrite(std::shared_ptr<DataBuffer> buffer) const
    {
        bool shouldSubmit = false;
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            sendQueue_.push_back(std::move(buffer));
            if (!writeInFlight_) {
                writeInFlight_ = true;
                shouldSubmit = true;
            }
        }

        if (!shouldSubmit) {
            return true;
        }

        if (SubmitNextStreamWrite()) {
            return true;
        }

        FailStreamWrite("Failed to submit write request");
        return false;
    }

    bool SubmitNextStreamWrite() const
    {
        std::shared_ptr<DataBuffer> buffer;
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            if (sendQueue_.empty()) {
                writeInFlight_ = false;
                return true;
            }
            buffer = sendQueue_.front();
        }

        auto self = shared_from_this();
        return IoUringManager::GetInstance().SubmitWriteRequest(
            NativeHandle(), buffer, [self](int, int res) { self->HandleStreamWriteComplete(res); });
    }

    void HandleStreamWriteComplete(int result) const
    {
        if (result < 0) {
            FailStreamWrite(std::string("Send error: ") + strerror(-result));
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
            FailStreamWrite("Send returned zero bytes");
            return;
        }

        if (shouldSubmitNext && !SubmitNextStreamWrite()) {
            FailStreamWrite("Failed to submit write request");
        }
    }

    void FailStreamWrite(const std::string &reason) const
    {
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            sendQueue_.clear();
            writeInFlight_ = false;
        }

        LMNET_LOGE("Stream send failed on fd %d: %s", NativeHandle(), reason.c_str());
        if (errorHandler_) {
            errorHandler_(NativeHandle(), reason);
        }
    }

    // Common Unix Socket send method
    bool SendUnixMessage(std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds) const
    {
        // Only duplicate file descriptors if we actually have them
        std::vector<int> duplicatedFds;
        if (!fds.empty()) {
            duplicatedFds = UnixSocketUtils::DuplicateFds(fds);
            if (duplicatedFds.empty()) {
                return false; // Failed to duplicate fds
            }
        }

        // Create a copy for cleanup callback before moving duplicatedFds
        std::vector<int> fdsForCleanup = duplicatedFds;
        auto cleanup = UnixSocketUtils::CreateCleanupCallback(std::move(fdsForCleanup), "Send Unix message");
        auto self = weak_from_this();
        bool submitted = IoUringManager::GetInstance().SubmitSendMsgRequest(
            NativeHandle(), buffer, duplicatedFds,
            [self, buffer, cleanup = std::move(cleanup)](int fd, int res) mutable {
                cleanup(fd, res);

                auto strong = self.lock();
                if (!strong) {
                    return;
                }

                if (res < 0) {
                    strong->FailStreamWrite(std::string("Send error: ") + strerror(-res));
                    return;
                }

                if (res == 0) {
                    strong->FailStreamWrite("Send returned zero bytes");
                    return;
                }

                if (buffer && static_cast<size_t>(res) < buffer->Size()) {
                    auto remaining = DataBuffer::PoolAlloc(buffer->Size() - static_cast<size_t>(res));
                    remaining->Assign(buffer->Data() + res, buffer->Size() - static_cast<size_t>(res));
                    if (!strong->QueueStreamWrite(std::move(remaining))) {
                        strong->FailStreamWrite("Failed to queue remaining Unix stream data");
                    }
                }
            });
        if (!submitted) {
            UnixSocketUtils::CleanupFds(duplicatedFds);
        }
        return submitted;
    }

private:
    TransportKind transportKind_ = TransportKind::TCP_STREAM;
    sockaddr_in client_addr_{};
    std::function<void(socket_t, const std::string &)> errorHandler_;
    mutable std::mutex sendMutex_;
    mutable std::deque<std::shared_ptr<DataBuffer>> sendQueue_;
    mutable bool writeInFlight_ = false;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_IO_URING_SESSION_IMPL_H
