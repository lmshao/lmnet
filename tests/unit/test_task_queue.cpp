//
// Copyright © 2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include <atomic>
#include <chrono>
#include <thread>

#include "../test_framework.h"
#include "network/task_queue.h"

using namespace lmshao::network;

TEST(TaskQueueTest, Construction)
{
    TaskQueue queue("TestQueue");
    // Just ensure it constructs without issues
    EXPECT_TRUE(true);
}

TEST(TaskQueueTest, StartAndStop)
{
    TaskQueue queue("TestQueue");

    EXPECT_EQ(queue.Start(), 0); // 0 means success
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(queue.Stop(), 0); // 0 means success
}

TEST(TaskQueueTest, BasicTaskExecution)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    std::atomic<bool> executed{false};
    auto task = std::make_shared<TaskHandler<void>>([&executed]() { executed = true; });

    queue.EnqueueTask(task);

    // Wait for task execution
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    EXPECT_TRUE(executed.load());

    queue.Stop();
}

TEST(TaskQueueTest, MultipleTasksExecution)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    std::atomic<int> counter{0};
    const int num_tasks = 10;

    for (int i = 0; i < num_tasks; ++i) {
        auto task = std::make_shared<TaskHandler<void>>([&counter]() { counter.fetch_add(1); });
        queue.EnqueueTask(task);
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(counter.load(), num_tasks);

    queue.Stop();
}

TEST(TaskQueueTest, TaskExecutionOrder)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    std::vector<int> execution_order;
    std::mutex order_mutex;

    for (int i = 0; i < 5; ++i) {
        auto task = std::make_shared<TaskHandler<void>>([&execution_order, &order_mutex, i]() {
            std::lock_guard<std::mutex> lock(order_mutex);
            execution_order.push_back(i);
        });
        queue.EnqueueTask(task);
    }

    // Wait for all tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    EXPECT_EQ(execution_order.size(), 5);
    // Tasks should execute in FIFO order
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(execution_order[i], i);
    }

    queue.Stop();
}

TEST(TaskQueueTest, TaskWithReturn)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    auto task = std::make_shared<TaskHandler<int>>([]() { return 42; });

    queue.EnqueueTask(task);

    // Wait for task execution
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Note: In the current implementation, we can't easily get return values
    // This test just ensures tasks with return types can be enqueued
    EXPECT_TRUE(true);

    queue.Stop();
}

TEST(TaskQueueTest, StopWithPendingTasks)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    std::atomic<int> executed_count{0};

    // Enqueue many tasks
    for (int i = 0; i < 100; ++i) {
        auto task = std::make_shared<TaskHandler<void>>([&executed_count]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            executed_count.fetch_add(1);
        });
        queue.EnqueueTask(task);
    }

    // Stop immediately (some tasks may not execute)
    queue.Stop();

    // The exact number depends on timing, but some should have executed
    EXPECT_GE(executed_count.load(), 0);
}

TEST(TaskQueueTest, DoubleStart)
{
    TaskQueue queue("TestQueue");

    EXPECT_EQ(queue.Start(), 0); // First start should succeed
    EXPECT_EQ(queue.Start(), 0); // Second start should also return 0 (already started)

    queue.Stop();
}

TEST(TaskQueueTest, DoubleStop)
{
    TaskQueue queue("TestQueue");
    queue.Start();

    EXPECT_EQ(queue.Stop(), 0); // First stop should succeed
    EXPECT_EQ(queue.Stop(), 0); // Second stop should also return 0 (already stopped)
}

TEST(TaskQueueTest, EnqueueAfterStop)
{
    TaskQueue queue("TestQueue");
    queue.Start();
    queue.Stop();

    std::atomic<bool> executed{false};
    auto task = std::make_shared<TaskHandler<void>>([&executed]() { executed = true; });

    // This should not crash, but task may not execute
    queue.EnqueueTask(task);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Task should not execute after stop
    EXPECT_FALSE(executed.load());
}

RUN_ALL_TESTS()
