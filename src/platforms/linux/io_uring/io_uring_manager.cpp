#include "platforms/linux/io_uring/io_uring_manager.h"

#include <liburing.h>
#include <sys/socket.h>

#include "internal_logger.h"

namespace lmshao::lmnet {

bool IoUringManager::Init(int entries)
{
    if (is_running_) {
        return true;
    }

    entries_ = entries;
    if (io_uring_queue_init(entries_, &ring_, 0) < 0) {
        LMNET_LOGE("io_uring_queue_init failed");
        return false;
    }

    request_pool_.resize(entries_);
    for (int i = 0; i < entries_; ++i) {
        free_requests_.push_back(&request_pool_[i]);
    }

    is_running_ = true;
    worker_thread_ = std::make_unique<std::thread>(&IoUringManager::Run, this);
    return true;
}

void IoUringManager::Stop()
{
    if (!is_running_) {
        return;
    }
    is_running_ = false;
    Exit();
}

void IoUringManager::Exit()
{
    Request *req = GetRequest();
    if (req) {
        req->event_type = EventType::EXIT;

        io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring_);
    }

    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    io_uring_queue_exit(&ring_);
}

void IoUringManager::Run()
{
    while (is_running_) {
        io_uring_cqe *cqe;

        int ret = io_uring_wait_cqe(&ring_, &cqe);
        if (ret < 0) {
            if (-ret == EINTR)
                continue;
            LMNET_LOGE("io_uring_wait_cqe failed: %s", strerror(-ret));
            continue;
        }

        Request *req = (Request *)io_uring_cqe_get_data(cqe);
        if (req->event_type == EventType::EXIT) {
            io_uring_cqe_seen(&ring_, cqe);
            break;
        }

        switch (req->event_type) {
            case EventType::ACCEPT:
                if (req->accept_callback) {
                    req->accept_callback(cqe->res, req->client_addr);
                }
                break;
            case EventType::CONNECT:
                if (req->connect_callback) {
                    req->connect_callback(req->fd, cqe->res);
                }
                break;
            case EventType::READ:
                if (req->read_callback) {
                    req->read_callback(req->fd, req->buffer, cqe->res);
                }
                break;
            case EventType::WRITE:
                if (req->write_callback) {
                    req->write_callback(req->fd, cqe->res);
                }
                break;
            case EventType::CLOSE:
                if (req->close_callback) {
                    req->close_callback(req->fd, cqe->res);
                }
                break;
            case EventType::RECVFROM:
                if (req->recvfrom_callback) {
                    req->recvfrom_callback(req->fd, req->buffer, cqe->res, req->client_addr);
                }
                break;
            default:
                break;
        }

        if (req->buffer) {
            req->buffer.reset();
        }

        io_uring_cqe_seen(&ring_, cqe);
        PutRequest(req);
    }
}

void IoUringManager::PutRequest(Request *req)
{
    if (req) {
        free_requests_.push_back(req);
    }
}

Request *IoUringManager::GetRequest()
{
    if (free_requests_.empty()) {
        return nullptr;
    }
    Request *req = free_requests_.back();
    free_requests_.pop_back();
    *req = Request();
    return req;
}

bool IoUringManager::SubmitConnectRequest(int fd, const sockaddr_in &addr, std::function<void(int, int)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::CONNECT;
    req->fd = fd;
    req->connect_callback = callback;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_connect(sqe, fd, (struct sockaddr *)&addr, sizeof(addr));
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitAcceptRequest(int fd, std::function<void(int, const sockaddr_in &)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::ACCEPT;
    req->fd = fd;
    req->accept_callback = callback;
    req->client_addr_len = sizeof(req->client_addr);

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_accept(sqe, fd, (struct sockaddr *)&req->client_addr, &req->client_addr_len, 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitReadRequest(int client_fd, std::shared_ptr<lmcore::DataBuffer> buffer,
                                       std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::READ;
    req->fd = client_fd;
    req->buffer = buffer;
    req->read_callback = callback;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_read(sqe, client_fd, buffer->Data(), buffer->Capacity(), 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitRecvFromRequest(
    int fd, std::shared_ptr<lmcore::DataBuffer> buffer,
    std::function<void(int, std::shared_ptr<lmcore::DataBuffer>, int, const sockaddr_in &)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::RECVFROM;
    req->fd = fd;
    req->buffer = buffer;
    req->recvfrom_callback = callback;
    req->client_addr_len = sizeof(req->client_addr);

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    msghdr msg{};
    iovec iov{};
    iov.iov_base = buffer->Data();
    iov.iov_len = buffer->Capacity();
    msg.msg_name = &req->client_addr;
    msg.msg_namelen = req->client_addr_len;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    io_uring_prep_recvmsg(sqe, fd, &msg, 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitSendToRequest(int fd, std::shared_ptr<lmcore::DataBuffer> buffer, const sockaddr_in &addr,
                                         std::function<void(int, int)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::WRITE;
    req->fd = fd;
    req->buffer = buffer;
    req->write_callback = callback;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    msghdr msg{};
    iovec iov{};
    iov.iov_base = buffer->Data();
    iov.iov_len = buffer->Size();
    msg.msg_name = (void *)&addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    io_uring_prep_sendmsg(sqe, fd, &msg, 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitWriteRequest(int client_fd, std::shared_ptr<lmcore::DataBuffer> buffer,
                                        std::function<void(int, int)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::WRITE;
    req->fd = client_fd;
    req->buffer = buffer;
    req->write_callback = callback;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_write(sqe, client_fd, buffer->Data(), buffer->Size(), 0);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

bool IoUringManager::SubmitCloseRequest(int client_fd, std::function<void(int, int)> callback)
{
    Request *req = GetRequest();
    if (!req) {
        LMNET_LOGE("Failed to get request");
        return false;
    }

    req->event_type = EventType::CLOSE;
    req->fd = client_fd;
    req->close_callback = callback;

    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    io_uring_prep_close(sqe, client_fd);
    io_uring_sqe_set_data(sqe, req);
    io_uring_submit(&ring_);
    return true;
}

} // namespace lmshao::lmnet
