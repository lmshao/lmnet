//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../test_framework.h"
#include "thread_pool.h"

using namespace lmshao::network;

TEST(ThreadPoolTest, BasicConstruction)
{
    ThreadPool pool(2, 5, "test");
    EXPECT_EQ(pool.GetThreadCount(), 2);
    EXPECT_EQ(pool.GetQueueSize(), 0);
}

TEST(ThreadPoolTest, DefaultConstruction)
{
    ThreadPool pool;
    EXPECT_EQ(pool.GetThreadCount(), THREAD_NUM_PRE_ALLOC);
    EXPECT_EQ(pool.GetQueueSize(), 0);
}

TEST(ThreadPoolTest, BasicTaskExecution)
{
    ThreadPool pool(1, 2, "test");
    std::atomic<bool> executed{false};

    pool.AddTask([&executed]() { executed = true; });

    // Wait for task to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, MultipleTasksExecution)
{
    ThreadPool pool(2, 4, "test");
    std::atomic<int> counter{0};
    const int numTasks = 10;

    for (int i = 0; i < numTasks; i++) {
        pool.AddTask([&counter]() { counter++; });
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(counter.load(), numTasks);
}

TEST(ThreadPoolTest, SerialTasksOrdering)
{
    ThreadPool pool(2, 4, "test");
    std::vector<int> results;
    std::mutex resultsMutex;

    const std::string serialTag = "test_serial";
    const int numTasks = 5;

    for (int i = 0; i < numTasks; i++) {
        pool.AddTask(
            [&results, &resultsMutex, i]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::lock_guard<std::mutex> lock(resultsMutex);
                results.push_back(i);
            },
            serialTag);
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    // Check that serial tasks executed in order
    EXPECT_EQ(results.size(), numTasks);
    for (int i = 0; i < numTasks; i++) {
        EXPECT_EQ(results[i], i);
    }
}

TEST(ThreadPoolTest, DifferentSerialTagsCanRunInParallel)
{
    ThreadPool pool(2, 4, "test");
    std::atomic<int> tag1_counter{0};
    std::atomic<int> tag2_counter{0};
    std::atomic<int> total_started{0};

    // Add tasks with different serial tags
    for (int i = 0; i < 3; i++) {
        pool.AddTask(
            [&tag1_counter, &total_started]() {
                total_started++;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                tag1_counter++;
            },
            "tag1");

        pool.AddTask(
            [&tag2_counter, &total_started]() {
                total_started++;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                tag2_counter++;
            },
            "tag2");
    }

    // Check that tasks from different tags can start in parallel
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_TRUE(total_started.load() >= 2); // At least 2 tasks should have started

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_EQ(tag1_counter.load(), 3);
    EXPECT_EQ(tag2_counter.load(), 3);
}

TEST(ThreadPoolTest, DynamicThreadCreation)
{
    ThreadPool pool(1, 3, "test");
    EXPECT_EQ(pool.GetThreadCount(), 1);

    std::atomic<int> running_tasks{0};
    std::atomic<int> max_concurrent{0};

    // Add tasks that will require more threads
    for (int i = 0; i < 3; i++) {
        pool.AddTask([&running_tasks, &max_concurrent]() {
            int current = running_tasks.fetch_add(1) + 1;

            // Update max concurrent if needed
            int expected = max_concurrent.load();
            while (expected < current && !max_concurrent.compare_exchange_weak(expected, current)) {
                expected = max_concurrent.load();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            running_tasks--;
        });
    }

    // Wait for thread creation and task execution
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(pool.GetThreadCount() > 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT_TRUE(max_concurrent.load() >= 2);
}

TEST(ThreadPoolTest, QueueSizeTracking)
{
    ThreadPool pool(1, 1, "test"); // Only 1 thread to force queueing

    std::atomic<bool> block_first_task{true};

    // Add a blocking task
    pool.AddTask([&block_first_task]() {
        while (block_first_task.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    // Add more tasks to queue
    for (int i = 0; i < 5; i++) {
        pool.AddTask([]() {
            // Quick task
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(pool.GetQueueSize() > 0);

    // Unblock and wait for completion
    block_first_task = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(pool.GetQueueSize(), 0);
}

TEST(ThreadPoolTest, SerialTaskQueueing)
{
    ThreadPool pool(1, 2, "test");
    std::vector<int> execution_order;
    std::mutex order_mutex;
    std::atomic<bool> block_first{true};

    const std::string serialTag = "blocking_serial";

    // Add blocking task
    pool.AddTask(
        [&block_first, &execution_order, &order_mutex]() {
            {
                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.push_back(1);
            }
            while (block_first.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        },
        serialTag);

    // Add more serial tasks (should queue)
    for (int i = 2; i <= 4; i++) {
        pool.AddTask(
            [&execution_order, &order_mutex, i]() {
                std::lock_guard<std::mutex> lock(order_mutex);
                execution_order.push_back(i);
            },
            serialTag);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(pool.GetQueueSize() > 0);

    // Unblock and check execution order
    block_first = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_EQ(execution_order.size(), 4);
    for (size_t i = 0; i < execution_order.size(); i++) {
        EXPECT_EQ(execution_order[i], static_cast<int>(i + 1));
    }
}

TEST(ThreadPoolTest, NullTaskHandling)
{
    ThreadPool pool(1, 2, "test");

    // This should not crash and should be handled gracefully
    pool.AddTask(nullptr);

    // Add a valid task to ensure pool is still functioning
    std::atomic<bool> executed{false};
    pool.AddTask([&executed]() { executed = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(executed.load());
}

TEST(ThreadPoolTest, ShutdownBehavior)
{
    auto pool = std::make_unique<ThreadPool>(2, 4, "test");
    std::atomic<int> completed_tasks{0};

    // Add some tasks
    for (int i = 0; i < 5; i++) {
        pool->AddTask([&completed_tasks]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            completed_tasks++;
        });
    }

    // Allow some tasks to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Shutdown should wait for running tasks to complete
    pool->Shutdown();

    // All started tasks should have completed
    EXPECT_TRUE(completed_tasks.load() > 0);

    // Try to add task after shutdown (should be rejected)
    std::atomic<bool> should_not_execute{false};
    pool->AddTask([&should_not_execute]() { should_not_execute = true; });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(should_not_execute.load());
}

TEST(ThreadPoolTest, ThreadNaming)
{
    ThreadPool pool(2, 4, "mypool");

    // This is more of a smoke test since we can't easily verify thread names
    // But we can ensure construction works with custom name
    EXPECT_EQ(pool.GetThreadCount(), 2);

    // Test with long name (should be truncated)
    ThreadPool pool2(1, 2, "verylongthreadpoolname");
    EXPECT_EQ(pool2.GetThreadCount(), 1);
}

TEST(ThreadPoolTest, MaxThreadsLimit)
{
    ThreadPool pool(1, 2, "test"); // Max 2 threads
    EXPECT_EQ(pool.GetThreadCount(), 1);

    std::atomic<int> running_tasks{0};

    // Add many tasks that will run concurrently
    for (int i = 0; i < 5; i++) {
        pool.AddTask([&running_tasks]() {
            running_tasks++;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            running_tasks--;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Should not exceed max threads
    EXPECT_TRUE(pool.GetThreadCount() <= 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

TEST(ThreadPoolTest, StressTest)
{
    ThreadPool pool(4, 8, "stress");
    std::atomic<int> counter{0};
    const int numTasks = 1000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numTasks; i++) {
        pool.AddTask([&counter]() {
            counter++;
            // Simulate some work
            volatile int x = 0;
            for (int j = 0; j < 1000; j++) {
                x += j;
            }
        });
    }

    // Wait for all tasks to complete
    while (counter.load() < numTasks) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    EXPECT_EQ(counter.load(), numTasks);
    EXPECT_TRUE(duration.count() < 5000); // Should complete within 5 seconds
    EXPECT_EQ(pool.GetQueueSize(), 0);
}

// Run all tests
RUN_ALL_TESTS()
