/**
 * @file test_data_buffer.cpp
 * @brief Data Buffer Unit Tests
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h> // for ntohs, ntohl on Windows
#else
#include <arpa/inet.h> // for ntohs, ntohl on Unix/Linux
#endif

#include <cstring>
#include <string>

#include "../test_framework.h"
#include "network/data_buffer.h"

using namespace lmshao::network;

TEST(DataBufferTest, Construction)
{
    DataBuffer buffer(1024);
    EXPECT_EQ(buffer.Capacity(), 1024);
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_TRUE(buffer.Data() != nullptr);
}

TEST(DataBufferTest, ZeroSizeConstruction)
{
    DataBuffer buffer(0);
    EXPECT_EQ(buffer.Capacity(), 0);
    EXPECT_EQ(buffer.Size(), 0);
}

TEST(DataBufferTest, AssignString)
{
    DataBuffer buffer(100);
    std::string test_data = "Hello, World!";

    buffer.Assign(test_data.c_str(), test_data.length());

    EXPECT_EQ(buffer.Size(), test_data.length());
    EXPECT_EQ(buffer.ToString(), test_data);
}

TEST(DataBufferTest, AssignBinaryData)
{
    DataBuffer buffer(100);
    uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    size_t data_size = sizeof(test_data);

    buffer.Assign(test_data, data_size);

    EXPECT_EQ(buffer.Size(), data_size);
    EXPECT_EQ(memcmp(buffer.Data(), test_data, data_size), 0);
}

TEST(DataBufferTest, AppendData)
{
    DataBuffer buffer(100);
    std::string first_part = "Hello";
    std::string second_part = ", World!";

    buffer.Assign(first_part.c_str(), first_part.length());
    buffer.Append(second_part.c_str(), second_part.length());

    EXPECT_EQ(buffer.Size(), first_part.length() + second_part.length());
    EXPECT_EQ(buffer.ToString(), first_part + second_part);
}

TEST(DataBufferTest, AutoResize)
{
    DataBuffer buffer(10);            // Small initial capacity
    std::string large_data(100, 'A'); // 100 characters

    buffer.Assign(large_data.c_str(), large_data.length());

    EXPECT_GE(buffer.Capacity(), large_data.length());
    EXPECT_EQ(buffer.Size(), large_data.length());
    EXPECT_EQ(buffer.ToString(), large_data);
}

TEST(DataBufferTest, AppendWithResize)
{
    DataBuffer buffer(5);
    std::string part1 = "ABC";
    std::string part2 = "DEFGHIJKLMNOP"; // This should trigger resize

    buffer.Assign(part1.c_str(), part1.length());
    buffer.Append(part2.c_str(), part2.length());

    EXPECT_GE(buffer.Capacity(), part1.length() + part2.length());
    EXPECT_EQ(buffer.ToString(), part1 + part2);
}

TEST(DataBufferTest, AssignUint16)
{
    DataBuffer buffer(100);
    uint16_t value = 0x1234;

    buffer.Assign(value);

    EXPECT_EQ(buffer.Size(), 2);
    // Check network byte order
    uint16_t stored_value = ntohs(*(uint16_t *)buffer.Data());
    EXPECT_EQ(stored_value, value);
}

TEST(DataBufferTest, AssignUint32)
{
    DataBuffer buffer(100);
    uint32_t value = 0x12345678;

    buffer.Assign(value);

    EXPECT_EQ(buffer.Size(), 4);
    // Check network byte order
    uint32_t stored_value = ntohl(*(uint32_t *)buffer.Data());
    EXPECT_EQ(stored_value, value);
}

TEST(DataBufferTest, AppendUint16)
{
    DataBuffer buffer(100);
    uint16_t value1 = 0x1234;
    uint16_t value2 = 0x5678;

    buffer.Assign(value1);
    buffer.Append(value2);

    EXPECT_EQ(buffer.Size(), 4);

    uint16_t stored_value1 = ntohs(*(uint16_t *)buffer.Data());
    uint16_t stored_value2 = ntohs(*(uint16_t *)(buffer.Data() + 2));
    EXPECT_EQ(stored_value1, value1);
    EXPECT_EQ(stored_value2, value2);
}

TEST(DataBufferTest, SetSize)
{
    DataBuffer buffer(100);

    buffer.SetSize(50);
    EXPECT_EQ(buffer.Size(), 50);
    EXPECT_GE(buffer.Capacity(), 50);

    // Test expanding beyond capacity
    buffer.SetSize(200);
    EXPECT_EQ(buffer.Size(), 200);
    EXPECT_GE(buffer.Capacity(), 200);
}

TEST(DataBufferTest, SetCapacity)
{
    DataBuffer buffer(50);

    buffer.SetCapacity(100);
    EXPECT_GE(buffer.Capacity(), 100);

    // Add some data
    std::string data = "test data";
    buffer.Assign(data.c_str(), data.length());

    // Shrink capacity (should preserve data)
    buffer.SetCapacity(20);
    EXPECT_EQ(buffer.ToString(), data);
}

TEST(DataBufferTest, Clear)
{
    DataBuffer buffer(100);
    std::string data = "test data";

    buffer.Assign(data.c_str(), data.length());
    EXPECT_EQ(buffer.Size(), data.length());

    buffer.Clear();
    EXPECT_EQ(buffer.Size(), 0);
    EXPECT_GT(buffer.Capacity(), 0); // Capacity should remain
}

TEST(DataBufferTest, CopyConstructor)
{
    DataBuffer original(100);
    std::string data = "test data";
    original.Assign(data.c_str(), data.length());

    DataBuffer copy(original);

    EXPECT_EQ(copy.Size(), original.Size());
    EXPECT_EQ(copy.ToString(), original.ToString());
}

TEST(DataBufferTest, AssignmentOperator)
{
    DataBuffer buffer1(100);
    DataBuffer buffer2(50);

    std::string data = "test data";
    buffer1.Assign(data.c_str(), data.length());

    buffer2 = buffer1;

    EXPECT_EQ(buffer2.Size(), buffer1.Size());
    EXPECT_EQ(buffer2.ToString(), buffer1.ToString());
}

TEST(DataBufferTest, MoveConstructor)
{
    DataBuffer original(100);
    std::string data = "test data";
    original.Assign(data.c_str(), data.length());

    size_t original_size = original.Size();
    std::string original_data = original.ToString();

    DataBuffer moved(std::move(original));

    EXPECT_EQ(moved.Size(), original_size);
    EXPECT_EQ(moved.ToString(), original_data);

    // Original should be empty after move
    EXPECT_EQ(original.Size(), 0);
    EXPECT_EQ(original.Capacity(), 0);
}

TEST(DataBufferTest, CreateFactory)
{
    auto buffer = DataBuffer::Create(160);
    EXPECT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->Capacity(), 160);
    EXPECT_EQ(buffer->Size(), 0);
}

RUN_ALL_TESTS()
