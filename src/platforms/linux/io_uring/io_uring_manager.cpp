#include "io_uring_manager.h"

#include <liburing.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>

#include "internal_logger.h"

namespace lmshao::lmnet {

IoUringManager::~IoUringManager()
{
    Stop();
}

bool IoUringManager::Init(int entries)
{
    if (isRunning_) {
        return true;
    }

    entries_ = entries;
    if (io_uring_queue_init(entries_, &ring_, 0) < 0) {
        LMNET_LOGE("io_uring_queue_init failed: %s", strerror(errno));
        return false;
    }
    LMNET_LOGI("io_uring initialized with %d entries", entries_);

    requestPool_.resize(entries_);
    for (int i = 0; i < entries_; ++i) {
        freeRequests_.push_back(&requestPool_[i]);
    }

    isRunning_ = true;
    workerThread_ = std::make_unique<std::thread>(&IoUringManager::Run, this);
    return true;
}

void IoUringManager::Stop()
{
    if (!isRunning_) {
        return;
    }
    isRunning_ = false;

    Exit();
}

void IoUringManager::Exit()
{
    Request *req = GetRequest();
    if (req) {
        req->event_type = RequestType::EXIT;

        std::lock_guard<std::mutex> lk(submitMutex_);
        io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring_);
    }

    if (workerThread_ && workerThread_->joinable()) {
        workerThread_->join();
    }
    io_uring_queue_exit(&ring_);
}

void IoUringManager::Run()
{
    while (isRunning_) {
        io_uring_cqe *cqe;

        int ret = io_uring_wait_cqe(&ring_, &cqe);

        if (ret < 0) {
            if (-ret == EINTR) {
                continue;
            }
            long tid_val = (long)syscall(SYS_gettid);
            LMNET_LOGE("io_uring_wait_cqe failed (tid=%ld): %s", tid_val, strerror(-ret));
            std::this_thread::sleep_for(std::chrono::microseconds(200)); // brief backoff to avoid tight spin
            continue;
        }

        Request *req = (Request *)io_uring_cqe_get_data(cqe);
        if (req->event_type == RequestType::EXIT) {
            io_uring_cqe_seen(&ring_, cqe);
            break;
        }

        // Use unified completion handling
        HandleCompletion(req, cqe->res);

        if (req->buffer) {
            req->buffer.reset();
        }

        io_uring_cqe_seen(&ring_, cqe);
        PutRequest(req);

        // Update completion statistics
        stats_.operations_completed.fetch_add(1);
    }
}

void IoUringManager::PutRequest(Request *req)
{
    if (req) {
        std::lock_guard<std::mutex> lk(poolMutex_);
        freeRequests_.push_back(req);
    }
}

bool IoUringManager::SubmitDirect()
{
    stats_.operations_submitted.fetch_add(1);
    int ret = io_uring_submit(&ring_);
    if (ret < 0) {
        LMNET_LOGE("io_uring_submit failed: %s", strerror(-ret));
        return false;
    }
    return true;
}

Request *IoUringManager::GetRequest()
{
    std::lock_guard<std::mutex> lk(poolMutex_);
    if (freeRequests_.empty()) {
        return nullptr;
    }
    Request *req = freeRequests_.back();
    freeRequests_.pop_back();
    req->Clear();
    return req;
}

void IoUringManager::HandleCompletion(Request *req, int result)
{
    switch (req->event_type) {
        case RequestType::CONNECT:
            if (req->connect_cb)
                req->connect_cb(req->fd, result);
            break;
        case RequestType::ACCEPT:
            if (req->accept_cb) {
                if (result >= 0) {
                    req->accept_cb(req->fd, result, (const sockaddr *)&req->client_addr, &req->client_addr_len);
                } else {
                    req->accept_cb(req->fd, result, nullptr, nullptr);
                }
            }
            break;
        case RequestType::READ:
            if (req->read_cb)
                req->read_cb(req->fd, req->buffer, result);
            break;
        case RequestType::WRITE:
            if (req->write_cb)
                req->write_cb(req->fd, result);
            break;
        case RequestType::CLOSE:
            if (req->close_cb)
                req->close_cb(req->fd, result);
            break;
        case RequestType::RECVFROM:
            if (req->recvfrom_cb) {
                req->recvfrom_cb(req->fd, req->buffer, result,
                                 *reinterpret_cast<const sockaddr_in *>(&req->client_addr));
            }
            break;
        case RequestType::SENDMSG:
            if (req->sendmsg_cb) {
                req->sendmsg_cb(req->fd, result);
            }
            break;
        case RequestType::RECVMSG:
            if (req->recvmsg_cb) {
                // Extract received file descriptors from control data
                req->received_fds.clear();
                if (result > 0 && req->msg.msg_controllen > 0) {
                    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(&req->msg); cmsg != nullptr;
                         cmsg = CMSG_NXTHDR(&req->msg, cmsg)) {
                        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                            size_t fdCount = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                            if (fdCount > 0) {
                                int *fds = reinterpret_cast<int *>(CMSG_DATA(cmsg));
                                req->received_fds.insert(req->received_fds.end(), fds, fds + fdCount);
                            }
                        }
                    }
                }
                req->recvmsg_cb(req->fd, req->buffer, result, req->received_fds);
            }
            break;
        case RequestType::EXIT:
            break;
    }
}

bool IoUringManager::SubmitOperation(RequestType type, int fd, const std::function<void(Request *)> &init_request,
                                     const std::function<void(io_uring_sqe *, Request *)> &prep_sqe)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = type;
    req->fd = fd;
    if (init_request)
        init_request(req);

    std::lock_guard<std::mutex> lk(submitMutex_);
    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        PutRequest(req);
        LMNET_LOGE("Failed to get SQE");
        return false;
    }
    if (prep_sqe)
        prep_sqe(sqe, req);
    io_uring_sqe_set_data(sqe, req);
    return SubmitDirect();
}

bool IoUringManager::SubmitConnectRequest(int fd, const sockaddr_in &addr, ConnectCallback callback)
{
    return SubmitOperation(
        RequestType::CONNECT, fd, [cb = std::move(callback)](Request *req) { req->connect_cb = cb; },
        [addr](io_uring_sqe *sqe, Request *req) {
            io_uring_prep_connect(sqe, req->fd, (struct sockaddr *)&addr, sizeof(addr));
        });
}

bool IoUringManager::SubmitAcceptRequest(int fd, AcceptCallback callback)
{
    return SubmitOperation(
        RequestType::ACCEPT, fd,
        [cb = std::move(callback)](Request *req) {
            req->accept_cb = cb;
            req->client_addr_len = sizeof(req->client_addr);
        },
        [](io_uring_sqe *sqe, Request *req) {
            io_uring_prep_accept(sqe, req->fd, (struct sockaddr *)&req->client_addr, &req->client_addr_len, 0);
        });
}

bool IoUringManager::SubmitReadRequest(int client_fd, std::shared_ptr<DataBuffer> buffer, ReadCallback callback)
{
    if (!buffer) {
        LMNET_LOGE("Buffer is nullptr in SubmitReadRequest");
        if (callback) {
            callback(client_fd, buffer, -EINVAL);
        }
        return false;
    }

    return SubmitOperation(
        RequestType::READ, client_fd,
        [cb = std::move(callback), buffer](Request *req) {
            req->read_cb = cb;
            req->buffer = buffer;
        },
        [buffer](io_uring_sqe *sqe, Request *req) {
            io_uring_prep_read(sqe, req->fd, buffer->Data(), buffer->Capacity(), 0);
        });
}

bool IoUringManager::SubmitRecvFromRequest(int fd, std::shared_ptr<DataBuffer> buffer, RecvFromCallback callback)
{
    if (!buffer) {
        LMNET_LOGE("Buffer is nullptr in SubmitRecvFromRequest");
        if (callback) {
            callback(fd, buffer, -EINVAL, {});
        }
        return false;
    }

    return SubmitOperation(
        RequestType::RECVFROM, fd,
        [cb = std::move(callback), buffer](Request *req) {
            req->recvfrom_cb = cb;
            req->buffer = buffer;
            req->client_addr_len = sizeof(req->client_addr);

            // Persist iov/msg inside Request to avoid stack lifetime issues
            req->iov.iov_base = buffer->Data();
            req->iov.iov_len = buffer->Capacity();
            req->msg = {};
            req->msg.msg_name = &req->client_addr;
            req->msg.msg_namelen = req->client_addr_len;
            req->msg.msg_iov = &req->iov;
            req->msg.msg_iovlen = 1;
        },
        [](io_uring_sqe *sqe, Request *req) { io_uring_prep_recvmsg(sqe, req->fd, &req->msg, 0); });
}

bool IoUringManager::SubmitSendToRequest(int fd, std::shared_ptr<DataBuffer> buffer, const sockaddr_in &addr,
                                         WriteCallback callback)
{
    if (!buffer) {
        LMNET_LOGE("Buffer is nullptr in SubmitSendToRequest");
        if (callback) {
            callback(fd, -EINVAL);
        }
        return false;
    }

    return SubmitOperation(
        RequestType::WRITE, fd,
        [cb = std::move(callback), buffer, addr](Request *req) {
            req->write_cb = cb;
            req->buffer = buffer;

            // Persist iov/msg inside Request to avoid stack lifetime issues
            req->iov.iov_base = buffer->Data();
            req->iov.iov_len = buffer->Size();
            req->msg = {};
            memcpy(&req->client_addr, &addr, sizeof(addr));
            req->msg.msg_name = &req->client_addr;
            req->msg.msg_namelen = sizeof(req->client_addr);
            req->msg.msg_iov = &req->iov;
            req->msg.msg_iovlen = 1;
            req->msg.msg_control = nullptr;
            req->msg.msg_controllen = 0;
            req->msg.msg_flags = 0;
        },
        [](io_uring_sqe *sqe, Request *req) { io_uring_prep_sendmsg(sqe, req->fd, &req->msg, 0); });
}

bool IoUringManager::SubmitWriteRequest(int client_fd, std::shared_ptr<DataBuffer> buffer, WriteCallback callback)
{
    if (!buffer) {
        LMNET_LOGE("Buffer is nullptr in SubmitWriteRequest");
        if (callback) {
            callback(client_fd, -EINVAL);
        }
        return false;
    }

    return SubmitOperation(
        RequestType::WRITE, client_fd,
        [cb = std::move(callback), buffer](Request *req) {
            req->write_cb = cb;
            req->buffer = buffer;
        },
        [buffer](io_uring_sqe *sqe, Request *req) {
            io_uring_prep_write(sqe, req->fd, buffer->Data(), buffer->Size(), 0);
        });
}

bool IoUringManager::SubmitCloseRequest(int client_fd, CloseCallback callback)
{
    return SubmitOperation(
        RequestType::CLOSE, client_fd, [cb = std::move(callback)](Request *req) { req->close_cb = cb; },
        [](io_uring_sqe *sqe, Request *req) { io_uring_prep_close(sqe, req->fd); });
}

// Unix Socket file descriptor transfer operations
bool IoUringManager::SubmitSendMsgRequest(int fd, std::shared_ptr<DataBuffer> buffer, const std::vector<int> &fds,
                                          SendMsgCallback callback)
{
    // Verify this is a Unix domain socket for FD passing
    if (!fds.empty()) {
        struct sockaddr_un addr;
        socklen_t len = sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &len) == 0) {
            if (addr.sun_family != AF_UNIX) {
                LMNET_LOGE("File descriptor passing is only supported on Unix domain sockets, not on AF_%d",
                           addr.sun_family);
                return false;
            }
        } else {
            LMNET_LOGW("Unable to verify socket family for FD passing");
        }
    }

    return SubmitOperation(
        RequestType::SENDMSG, fd,
        [cb = std::move(callback), buffer, fds](Request *req) {
            req->sendmsg_cb = cb;
            req->buffer = buffer;
            req->fds_to_send = fds;

            // Setup iovec
            req->iov.iov_base = buffer ? buffer->Data() : nullptr;
            req->iov.iov_len = buffer ? buffer->Size() : 0;

            // If no data, send a single placeholder byte using Request member
            if (!buffer || buffer->Size() == 0) {
                req->placeholder_byte = 0;
                req->iov.iov_base = &req->placeholder_byte;
                req->iov.iov_len = 1;
            }

            // Setup msghdr
            req->msg = {};
            req->msg.msg_iov = &req->iov;
            req->msg.msg_iovlen = 1;

            // Setup control data for file descriptors
            if (!fds.empty()) {
                req->control_buffer.resize(CMSG_SPACE(sizeof(int) * fds.size()));
                req->msg.msg_control = req->control_buffer.data();
                req->msg.msg_controllen = req->control_buffer.size();

                struct cmsghdr *cmsg = CMSG_FIRSTHDR(&req->msg);
                cmsg->cmsg_level = SOL_SOCKET;
                cmsg->cmsg_type = SCM_RIGHTS;
                cmsg->cmsg_len = CMSG_LEN(sizeof(int) * fds.size());
                std::memcpy(CMSG_DATA(cmsg), fds.data(), sizeof(int) * fds.size());
            }
        },
        [](io_uring_sqe *sqe, Request *req) { io_uring_prep_sendmsg(sqe, req->fd, &req->msg, MSG_NOSIGNAL); });
}

bool IoUringManager::SubmitRecvMsgRequest(int fd, std::shared_ptr<DataBuffer> buffer, RecvMsgCallback callback)
{
    if (!buffer) {
        LMNET_LOGE("Buffer is nullptr in SubmitRecvMsgRequest");
        if (callback) {
            callback(fd, buffer, -EINVAL, {});
        }
        return false;
    }

    constexpr int MAX_FDS_PER_MESSAGE = 32;

    return SubmitOperation(
        RequestType::RECVMSG, fd,
        [cb = std::move(callback), buffer](Request *req) {
            req->recvmsg_cb = cb;
            req->buffer = buffer;
            req->received_fds.clear();

            // Setup iovec
            req->iov.iov_base = buffer->Data();
            req->iov.iov_len = buffer->Capacity();

            // Setup msghdr
            req->msg = {};
            req->msg.msg_iov = &req->iov;
            req->msg.msg_iovlen = 1;

            // Setup control buffer for receiving file descriptors
            req->control_buffer.resize(CMSG_SPACE(sizeof(int) * MAX_FDS_PER_MESSAGE));
            req->msg.msg_control = req->control_buffer.data();
            req->msg.msg_controllen = req->control_buffer.size();
        },
        [](io_uring_sqe *sqe, Request *req) { io_uring_prep_recvmsg(sqe, req->fd, &req->msg, 0); });
}

} // namespace lmshao::lmnet
