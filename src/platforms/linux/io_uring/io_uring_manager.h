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
#include <netinet/in.h>
#include <sys/socket.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "lmcore/data_buffer.h"
#include "lmcore/singleton.h"
#include "lmnet/common.h"

namespace lmshao::lmnet {

enum class EventType {
    ACCEPT,
    CONNECT,
    READ,
    WRITE,
    CLOSE,
    RECVFROM,
    EXIT,
};

struct Request {
    int fd;
    EventType event_type;
    std::function<void(int, int)> connect_callback;
    std::function<void(int, const sockaddr_in &)> accept_callback;
    std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int)> read_callback;
    std::function<void(int, int)> write_callback;
    std::function<void(int, int)> close_callback;
    std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int, const sockaddr_in &)> recvfrom_callback;
    std::shared_ptr<lmcore::DataBuffer> buffer;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    iovec iov;

    Request() : fd(-1), event_type(EventType::ACCEPT), buffer(nullptr), client_addr_len(sizeof(sockaddr_in)) {}
};

class IoUringManager : public lmcore::Singleton<IoUringManager> {
public:
    bool Init(int entries = 256);
    void Stop();
    void Exit();

    bool SubmitConnectRequest(int fd, const sockaddr_in &addr, std::function<void(int, int)> callback);
    bool SubmitAcceptRequest(int fd, std::function<void(int, const sockaddr_in &)> callback);
    bool SubmitReadRequest(int client_fd, std::shared_ptr<lmcore::DataBuffer> buffer,
                           std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int)> callback);
    bool SubmitRecvFromRequest(
        int fd, std::shared_ptr<lmcore::DataBuffer> buffer,
        std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int, const sockaddr_in &)> callback);
    bool SubmitSendToRequest(int fd, std::shared_ptr<lmcore::DataBuffer> buffer, const sockaddr_in &addr,
                             std::function<void(int, int)> callback);
    bool SubmitWriteRequest(int client_fd, std::shared_ptr<lmcore::DataBuffer> buffer,
                            std::function<void(int, int)> callback);
    bool SubmitCloseRequest(int client_fd, std::function<void(int, int)> callback);

private:
    void Run();
    void PutRequest(Request *req);
    Request *GetRequest();

private:
    IoUringManager() = default;
    friend class lmcore::Singleton<IoUringManager>;

private:
    struct io_uring ring_;
    int entries_;
    std::vector<Request> request_pool_;
    std::vector<Request *> free_requests_;
    std::atomic_bool is_running_{false};
    std::unique_ptr<std::thread> worker_thread_;
};

} // namespace lmshao::lmnet

#endif // LMSHAO_LMNET_LINUX_IO_URING_MANAGER_H
