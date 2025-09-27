/**
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LMSHAO_LMNET_LINUX_IO_URING_UNIX_CLIENT_IMPL_H
#define LMSHAO_LMNET_LINUX_IO_URING_UNIX_CLIENT_IMPL_H

#include "lmnet/unix_client.h"
#include "lmnet/common.h"
#include "i_client_listener.h"
#include "lmnet/data_buffer.h"
#include "io_uring_manager.h"
#include <atomic>
#include <memory>
#include <string>

namespace lmshao {
namespace lmnet {

class UnixClientImpl : public IUnixClient {
  public:
    explicit UnixClientImpl(std::string server_path);
    ~UnixClientImpl() override;

    bool Init() override;
    void Stop() override;
    bool Send(std::shared_ptr<lmcore::DataBuffer> data) override;
    void SetListener(std::shared_ptr<IClientListener> listener) override { listener_ = listener; }

  private:
    void OnConnect(int res);
    void OnRead(std::shared_ptr<lmcore::DataBuffer> data, int res);
    void OnWrite(int res);

  private:
    std::string server_path_;
    int socket_ = -1;
    std::weak_ptr<IClientListener> listener_;
    std::atomic_bool is_running_{false};
};

} // namespace lmnet
} // namespace lmshao

#endif // LMSHAO_LMNET_LINUX_IO_URING_UNIX_CLIENT_IMPL_H
