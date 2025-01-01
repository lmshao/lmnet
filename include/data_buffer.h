//
// Copyright © 2023-2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_DATA_BUFFER_H
#define NETWORK_DATA_BUFFER_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

class DataBuffer {
public:
    explicit DataBuffer(size_t len = 0);
    DataBuffer(const DataBuffer &other) noexcept;
    DataBuffer &operator=(const DataBuffer &other) noexcept;
    DataBuffer(DataBuffer &&other) noexcept;
    DataBuffer &operator=(DataBuffer &&other) noexcept;

    virtual ~DataBuffer();

    static std::shared_ptr<DataBuffer> Create(size_t len = 0);

    void Assign(std::nullptr_t) {}
    void Assign(const void *p, size_t len);
    void Assign(uint8_t c) { Assign((uint8_t *)&c, 1); }
    void Assign(uint16_t u16);
    void Assign(uint32_t u32);
    void Assign(const char *str) { Assign(str, strlen(str)); }
    void Assign(const std::string &s) { Assign(s.c_str(), s.size()); }

    void Append(std::nullptr_t) {}
    void Append(const void *p, size_t len);
    void Append(uint8_t c) { Append((uint8_t *)&c, 1); }
    void Append(uint16_t u16);
    void Append(uint32_t u32);
    void Append(const char *str) { Append(str, strlen(str)); }
    void Append(const std::string &s) { Append(s.c_str(), s.size()); }
    void Append(std::shared_ptr<DataBuffer> b) { Append(b->Data(), b->Size()); }

    uint8_t *Data() { return data_; }

    size_t Size() const { return size_; }
    void SetSize(size_t len);

    size_t Capacity() const { return capacity_; }
    void SetCapacity(size_t len);

    bool Empty() const { return size_ == 0; }
    void Clear();
    void HexDump(size_t len = 0);
    std::string ToString();

private:
    uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t capacity_ = 0;
};

#endif // NETWORK_DATA_BUFFER_H