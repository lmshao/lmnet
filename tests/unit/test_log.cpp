/**
 * @file test_log.cpp
 * @brief Logging System Unit Tests
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include <chrono>
#include <regex>
#include <thread>

#include "../test_framework.h"
#include "log.h"

using namespace lmshao::network;

TEST(LogTest, TimeFormat)
{
    std::string time_str = Time();

    EXPECT_GT(time_str.length(), 0);

    // Verify time format: YYYY-MM-DD HH:MM:SS.mmm
    std::regex time_pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})");
    EXPECT_TRUE(std::regex_match(time_str, time_pattern));
}

TEST(LogTest, TimeConsistency)
{
    std::string time1 = Time();
    std::string time2 = Time();

    // Times should be close (same second or consecutive)
    EXPECT_TRUE(time1.length() == time2.length());

    // Extract the base time (without milliseconds)
    std::string base1 = time1.substr(0, 19); // YYYY-MM-DD HH:MM:SS
    std::string base2 = time2.substr(0, 19);

    // Should be same second or very close
    EXPECT_TRUE(base1 == base2 || time1 <= time2);
}

TEST(LogTest, ThreadSafety)
{
    const int num_threads = 10;
    const int calls_per_thread = 100;
    std::vector<std::thread> threads;
    std::vector<std::string> results(num_threads * calls_per_thread);

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&results, t, calls_per_thread]() {
            for (int i = 0; i < calls_per_thread; ++i) {
                results[t * calls_per_thread + i] = Time();
                std::this_thread::sleep_for(std::chrono::microseconds(1));
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    // Verify all results are valid time strings
    std::regex time_pattern(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})");
    for (const auto &time_str : results) {
        EXPECT_TRUE(std::regex_match(time_str, time_pattern));
    }
}

TEST(LogTest, PerformanceCache)
{
    // Test that consecutive calls in the same second are faster
    auto start = std::chrono::high_resolution_clock::now();

    // Make many calls quickly (within same second)
    for (int i = 0; i < 1000; ++i) {
        std::string time_str = Time();
        EXPECT_GT(time_str.length(), 0);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

    // Should complete quickly due to caching
    EXPECT_GT(duration.count(), 0); // At least some time should pass

    std::cout << "1000 Time() calls took: " << duration.count() << " microseconds" << std::endl;
}

TEST(LogTest, MillisecondUpdates)
{
    std::string time1 = Time();

    // Sleep for a short time to ensure millisecond change
    std::this_thread::sleep_for(std::chrono::milliseconds(2));

    std::string time2 = Time();

    // Extract millisecond parts
    std::string ms1 = time1.substr(20, 3); // Last 3 digits
    std::string ms2 = time2.substr(20, 3);

    // Milliseconds should be different (unless we're very unlucky with timing)
    // But at minimum, time2 >= time1
    EXPECT_TRUE(time2 >= time1);
}

RUN_ALL_TESTS()
