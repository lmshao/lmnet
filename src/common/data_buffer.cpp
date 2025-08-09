/**
 * @file data_buffer.cpp
 * @brief Data Buffer Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "data_buffer.h"

#include <arpa/inet.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace lmshao::network {
constexpr size_t DATA_ALIGN = 8;
inline static size_t align(size_t len)
{
    return (len + DATA_ALIGN - 1) / DATA_ALIGN * DATA_ALIGN;
}

constexpr size_t POOL_BLOCK_SIZE = 4096;
constexpr size_t POOL_GLOBAL_MAX = 1024;
constexpr size_t POOL_LOCAL_MAX = 32;
static std::mutex g_poolMutex;
static std::vector<DataBuffer *> g_bufferPool;
thread_local std::vector<DataBuffer *> t_localPool;

DataBuffer::DataBuffer(size_t len)
{
    if (len) {
        capacity_ = align(len);
        data_ = new uint8_t[capacity_];
        size_ = 0;
        data_[0] = 0;
    }
}

DataBuffer::DataBuffer(const DataBuffer &other) noexcept
{
    if (other.data_ && other.size_) {
        capacity_ = align(other.size_);
        data_ = new uint8_t[capacity_];
        memcpy(data_, other.data_, other.size_);
        size_ = other.size_;
        if (size_ < capacity_) {
            data_[size_] = 0;
        }
    }
}

DataBuffer &DataBuffer::operator=(const DataBuffer &other) noexcept
{
    if (this != &other) {
        delete[] data_;
        if (other.data_ && other.size_) {
            capacity_ = align(other.size_);
            data_ = new uint8_t[capacity_];
            memcpy(data_, other.data_, other.size_);
            size_ = other.size_;
            if (size_ < capacity_) {
                data_[size_] = 0;
            }
        } else {
            data_ = nullptr;
            size_ = 0;
            capacity_ = 0;
        }
    }
    return *this;
}

DataBuffer::DataBuffer(DataBuffer &&other) noexcept
{
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

DataBuffer &DataBuffer::operator=(DataBuffer &&other) noexcept
{
    if (this != &other) {
        delete[] data_;
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    return *this;
}

DataBuffer::~DataBuffer()
{
    delete[] data_;
    data_ = nullptr;
    capacity_ = 0;
    size_ = 0;
}

std::shared_ptr<DataBuffer> DataBuffer::Create(size_t len)
{
    return std::make_shared<DataBuffer>(len);
}

std::shared_ptr<DataBuffer> DataBuffer::PoolAlloc(size_t len)
{
    DataBuffer *buf = nullptr;
    if (len > POOL_BLOCK_SIZE) {
        buf = new DataBuffer(len);
        return std::shared_ptr<DataBuffer>(buf, [](DataBuffer *p) { delete p; });
    }

    if (!t_localPool.empty()) {
        buf = t_localPool.back();
        t_localPool.pop_back();
        buf->SetSize(0);
    } else {
        std::lock_guard<std::mutex> lock(g_poolMutex);
        if (!g_bufferPool.empty()) {
            buf = g_bufferPool.back();
            g_bufferPool.pop_back();
            buf->SetSize(0);
        }
    }

    if (!buf) {
        buf = new DataBuffer(POOL_BLOCK_SIZE);
    }
    return std::shared_ptr<DataBuffer>(buf, [](DataBuffer *p) { DataBuffer::PoolFree(p); });
}

void DataBuffer::PoolFree(DataBuffer *buf)
{
    if (!buf)
        return;
    if (buf->capacity_ != POOL_BLOCK_SIZE) {
        delete buf;
        return;
    }

    if (t_localPool.size() < POOL_LOCAL_MAX) {
        buf->Clear();
        t_localPool.push_back(buf);
        return;
    }

    std::lock_guard<std::mutex> lock(g_poolMutex);
    if (g_bufferPool.size() < POOL_GLOBAL_MAX) {
        buf->Clear();
        g_bufferPool.push_back(buf);
    } else {
        delete buf;
    }
}

void DataBuffer::Assign(const void *p, size_t len)
{
    if (!p || !len) {
        return;
    }

    if (len > capacity_) {
        delete[] data_;
        capacity_ = align(len);
        data_ = new uint8_t[capacity_];
    }

    memcpy(data_, p, len);
    size_ = len;
    if (size_ < capacity_) {
        data_[size_] = 0;
    }
}

void DataBuffer::Append(const void *p, size_t len)
{
    if (!p || !len) {
        return;
    }

    if (len + size_ <= capacity_) {
        memcpy(data_ + size_, p, len);
        size_ += len;
    } else {
        capacity_ = align(size_ + len);
        auto newBuffer = new uint8_t[capacity_];
        if (data_) {
            memcpy(newBuffer, data_, size_);
        }

        memcpy(newBuffer + size_, p, len);
        delete[] data_;
        data_ = newBuffer;
        size_ += len;
    }

    if (size_ < capacity_) {
        data_[size_] = 0;
    }
}

void DataBuffer::Assign(uint16_t u16)
{
    uint16_t t = htons(u16);
    Assign((uint8_t *)&t, 2);
}

void DataBuffer::Assign(uint32_t u32)
{
    uint32_t t = htonl(u32);
    Assign((uint8_t *)&t, 4);
}

void DataBuffer::Append(uint16_t u16)
{
    uint16_t t = htons(u16);
    Append((uint8_t *)&t, 2);
}

void DataBuffer::Append(uint32_t u32)
{
    uint32_t t = htonl(u32);
    Append((uint8_t *)&t, 4);
}

void DataBuffer::SetSize(size_t len)
{
    if (len <= capacity_) {
        size_ = len;
        if (data_ && size_ < capacity_) {
            data_[size_] = 0;
        }
        return;
    }

    capacity_ = align(len);
    auto new_buffer = new uint8_t[capacity_];
    if (data_) {
        memcpy(new_buffer, data_, size_);
    }

    delete[] data_;
    data_ = new_buffer;
    size_ = len;
    if (size_ < capacity_) {
        data_[size_] = 0;
    }
}

void DataBuffer::SetCapacity(size_t len)
{
    if (align(len) == capacity_) {
        return;
    }

    if (size_ == 0) {
        delete[] data_;
        capacity_ = align(len);
        data_ = new uint8_t[capacity_];
        size_ = 0;
    } else {
        capacity_ = align(len);
        if (size_ <= capacity_) {
            auto newBuffer = new uint8_t[capacity_];
            if (data_) {
                memcpy(newBuffer, data_, size_);
            }
            delete[] data_;
            data_ = newBuffer;
        } else {
            auto newBuffer = new uint8_t[capacity_];
            if (data_) {
                memcpy(newBuffer, data_, capacity_);
            }
            delete[] data_;
            data_ = newBuffer;
            size_ = capacity_;
        }
    }
}

void DataBuffer::Clear()
{
    size_ = 0;
    if (data_ && capacity_ > 0) {
        data_[0] = 0;
    }
}

void DataBuffer::HexDump(size_t len)
{
    if (len == 0 || len > size_) {
        len = size_;
    }

    size_t i;
    for (i = 0; i < len; ++i) {
        printf("%02x ", *(data_ + i));
        if (i % 16 == 15) {
            printf("\n");
        }
    }

    if (i % 16 != 0) {
        printf("\n");
    }
}

std::string DataBuffer::ToString()
{
    if (!data_ || size_ == 0) {
        return {};
    }
    return std::string((char *)data_, size_);
}

bool DataBuffer::operator==(const DataBuffer &other) const
{
    if (size_ != other.size_) {
        return false;
    }

    if (size_ == 0) {
        return true;
    }

    return memcmp(data_, other.data_, size_) == 0;
}
} // namespace lmshao::network