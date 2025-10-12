/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_IO_URING_MANAGER_H
#define LMSHAO_LMNET_LINUX_IO_URING_MANAGER_H

#include <arpa/inet.h>
#include <liburing.h>
#include <lmcore/singleton.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "lmnet/common.h"

namespace lmshao::lmnet {
using lmshao::lmcore::Singleton;

enum class RequestType {
    ACCEPT,
    CONNECT,
    READ,
    WRITE,
    CLOSE,
    RECVFROM,
    EXIT
};

using ConnectCallback = std::function<void(int, int)>;                               // fd, result
using AcceptCallback = std::function<void(int, int, const sockaddr *, socklen_t *)>; // listen_fd, client_fd, addr, len
using ReadCallback = std::function<void(int, std::shared_ptr<DataBuffer>, int)>;     // fd, buffer, bytes/err
using WriteCallback = std::function<void(int, int)>;                                 // fd, bytes/err
using CloseCallback = std::function<void(int, int)>;                                 // fd, result
using RecvFromCallback = std::function<void(int, std::shared_ptr<DataBuffer>, int,
                                            const sockaddr_in &)>; // fd, buffer, bytes/err, addr

struct Request {
    int fd;
    RequestType event_type;
    ConnectCallback connect_cb;
    AcceptCallback accept_cb;
    ReadCallback read_cb;
    WriteCallback write_cb;
    CloseCallback close_cb;
    RecvFromCallback recvfrom_cb;
    std::shared_ptr<DataBuffer> buffer;
    struct sockaddr_storage client_addr;
    socklen_t client_addr_len;
    struct iovec iov;
    struct msghdr msg;

    Request() : fd(-1), event_type(RequestType::ACCEPT), client_addr_len(sizeof(sockaddr_storage)), iov{}, msg{} {}

    // Clear the request for reuse
    void Clear()
    {
        fd = -1;
        event_type = RequestType::ACCEPT;
        connect_cb = nullptr;
        accept_cb = nullptr;
        read_cb = nullptr;
        write_cb = nullptr;
        close_cb = nullptr;
        recvfrom_cb = nullptr;
        buffer.reset();
        client_addr_len = sizeof(sockaddr_storage);
        iov = {};
        msg = {};
    }
};

class IoUringManager : public Singleton<IoUringManager> {
public:
    ~IoUringManager();
    bool Init(int entries = 256);
    void Stop();
    void Exit();

    // Performance stats
    struct IoUringStats {
        std::atomic<uint64_t> operations_submitted{0};
        std::atomic<uint64_t> operations_completed{0};
        std::atomic<uint64_t> buffer_reuses{0};
    };
    const IoUringStats &GetStats() const { return stats_; }

    bool SubmitConnectRequest(int fd, const sockaddr_in &addr, ConnectCallback callback);
    bool SubmitAcceptRequest(int fd, AcceptCallback callback);
    bool SubmitReadRequest(int client_fd, std::shared_ptr<DataBuffer> buffer, ReadCallback callback);
    bool SubmitRecvFromRequest(int fd, std::shared_ptr<DataBuffer> buffer, RecvFromCallback callback);
    bool SubmitSendToRequest(int fd, std::shared_ptr<DataBuffer> buffer, const sockaddr_in &addr,
                             WriteCallback callback);
    bool SubmitWriteRequest(int client_fd, std::shared_ptr<DataBuffer> buffer, WriteCallback callback);
    bool SubmitCloseRequest(int client_fd, CloseCallback callback);

private:
    void Run();
    void PutRequest(Request *req);
    Request *GetRequest();
    bool SubmitDirect();

    bool SubmitOperation(RequestType type, int fd, const std::function<void(Request *)> &init_request,
                         const std::function<void(io_uring_sqe *, Request *)> &prep_sqe);

    void HandleCompletion(Request *req, int result);

private:
    IoUringManager() = default;
    friend class Singleton<IoUringManager>;

private:
    struct io_uring ring_;
    int entries_;
    std::vector<Request> requestPool_;
    std::vector<Request *> freeRequests_;
    std::mutex poolMutex_;
    std::mutex submitMutex_;
    std::atomic_bool isRunning_{false};
    std::unique_ptr<std::thread> workerThread_;

    mutable IoUringStats stats_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_IO_URING_MANAGER_H
