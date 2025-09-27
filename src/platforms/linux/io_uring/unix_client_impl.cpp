#include "unix_client_impl.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "internal_logger.h"

namespace lmshao::lmnet {

UnixClientImpl::UnixClientImpl(const std::string& server_path) : server_path_(server_path) {}

UnixClientImpl::~UnixClientImpl() {
    Stop();
}

bool UnixClientImpl::Init() {
    if (is_running_) {
        return true;
    }

    socket_ = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_ < 0) {
        LMNET_LOGE("Failed to create unix socket");
        return false;
    }

    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, server_path_.c_str(), sizeof(server_addr.sun_path) - 1);

    IoUringManager::GetInstance().Init();
    IoUringManager::GetInstance().SubmitConnectRequest(
        socket_, *reinterpret_cast<const sockaddr_in*>(&server_addr),
        [this](int, int res) { OnConnect(res); });

    is_running_ = true;
    return true;
}

void UnixClientImpl::Stop() {
    if (!is_running_) {
        return;
    }
    is_running_ = false;

    if (socket_ != -1) {
        IoUringManager::GetInstance().SubmitCloseRequest(socket_, nullptr);
        socket_ = -1;
    }
}

bool UnixClientImpl::Send(std::shared_ptr<lmcore::DataBuffer> data) {
    if (!is_running_) {
        return false;
    }
    return IoUringManager::GetInstance().SubmitWriteRequest(socket_, data, [this](int, int res) { OnWrite(res); });
}

void UnixClientImpl::OnConnect(int res) {
    if (res < 0) {
        LMNET_LOGE("Failed to connect: %s", strerror(-res));
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, strerror(-res));
        }
        Stop();
        return;
    }

    LMNET_LOGI("Unix client connected");

    auto buffer = std::make_shared<lmcore::DataBuffer>(4096);
    IoUringManager::GetInstance().SubmitReadRequest(
        socket_, buffer, [this, buffer](int, std::shared_ptr<lmcore::DataBuffer>, int read_res) { OnRead(buffer, read_res); });
}

void UnixClientImpl::OnRead(std::shared_ptr<lmcore::DataBuffer> data, int res) {
    if (res <= 0) {
        if (res < 0) {
            LMNET_LOGE("Read error: %s", strerror(-res));
            if (auto listener = listener_.lock()) {
                listener->OnError(socket_, strerror(-res));
            }
        } else {
            LMNET_LOGI("Connection closed by peer");
        }
        if (auto listener = listener_.lock()) {
            listener->OnClose(socket_);
        }
        Stop();
        return;
    }

    data->SetSize(res);
    if (auto listener = listener_.lock()) {
        listener->OnReceive(socket_, data);
    }

    // Continue reading
    auto buffer = std::make_shared<lmcore::DataBuffer>(4096);
    IoUringManager::GetInstance().SubmitReadRequest(
        socket_, buffer, [this, buffer](int, std::shared_ptr<lmcore::DataBuffer>, int read_res) { OnRead(buffer, read_res); });
}

void UnixClientImpl::OnWrite(int res) {
    if (res < 0) {
        LMNET_LOGE("Write error: %s", strerror(-res));
        if (auto listener = listener_.lock()) {
            listener->OnError(socket_, strerror(-res));
        }
    }
}

} // namespace lmshao::lmnet
