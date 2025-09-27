/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_IO_URING_UNIX_SERVER_IMPL_H
#define LMSHAO_LMNET_LINUX_IO_URING_UNIX_SERVER_IMPL_H

#include "lmnet/unix_server.h"
#include "lmnet/common.h"
#include "i_server_listener.h"
#include "lmnet/data_buffer.h"
#include "io_uring_manager.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <sys/socket.h>

namespace lmshao {
namespace lmnet {

class UnixServerImpl : public IUnixServer {
  public:
    explicit UnixServerImpl(std::string path);
    ~UnixServerImpl() override;

    bool Init() override;
    void Stop() override;
    void SetListener(IServerListener* listener) override { listener_ = listener; }

  private:
    void OnAccept(int client_fd, const sockaddr_in& client_addr);
    void OnRead(int client_fd, std::shared_ptr<lmcore::DataBuffer> data, int res);

  private:
    std::string path_;
    int socket_ = -1;
    IServerListener* listener_ = nullptr;
    std::atomic_bool is_running_{false};
    std::vector<int> client_fds_;
};

} // namespace lmnet
} // namespace lmshao

#endif // LMSHAO_LMNET_LINUX_IO_URING_UNIX_SERVER_IMPL_H
