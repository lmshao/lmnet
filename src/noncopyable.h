//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_NONCOPYABLE_H
#define NETWORK_NONCOPYABLE_H

namespace lmshao::network {
class NonCopyable {
protected:
    NonCopyable() = default;
    virtual ~NonCopyable() = default;

public:
    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;
    NonCopyable(NonCopyable &&) = delete;
    NonCopyable &operator=(NonCopyable &&) = delete;
};

} // namespace lmshao::network

#endif // NETWORK_NONCOPYABLE_H