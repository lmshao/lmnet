//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_SESSION_H
#define NETWORK_SESSION_H

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

#include "base_server.h"
#include "data_buffer.h"

class Session {
public:
    std::string host;
    uint16_t port;
    int fd = 0;

    virtual bool Send(std::shared_ptr<DataBuffer> buffer) const = 0;
    virtual bool Send(const std::string &str) const = 0;
    virtual std::string ClientInfo() const = 0;

protected:
    Session() = default;
    virtual ~Session() = default;
};

#endif // NETWORK_SESSION_H