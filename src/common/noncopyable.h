/**
 * @file noncopyable.h
 * @brief Non-copyable Base Class
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

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