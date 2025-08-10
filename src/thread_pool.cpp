/**
 * @file thread_pool.cpp
 * @brief Thread Pool Implementation
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#include "thread_pool.h"

#include <string>
#include <utility>

#include "log.h"

namespace lmshao::network {
constexpr size_t POOL_SIZE_MAX = 100;

ThreadPool::ThreadPool(int preAlloc, int threadsMax, std::string name) : threadsMax_(threadsMax)
{
    if (preAlloc > threadsMax) {
        preAlloc = threadsMax;
    }

    if (!name.empty()) {
        if (name.size() > 12) {
            name = name.substr(0, 12);
        }
        threadName_ = name;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < preAlloc; ++i) {
        CreateWorkerThread();
    }
}

ThreadPool::~ThreadPool()
{
    Shutdown();
}

void ThreadPool::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        shutdown_ = true;
    }

    signal_.notify_all();
    for (auto &thread : threads_) {
        if (thread->joinable()) {
            thread->join();
        }
    }
}

void ThreadPool::Worker()
{
    while (running_) {
        std::shared_ptr<TaskItem> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            idle_++;
            signal_.wait(lock, [this] { return !running_ || !tasks_.empty() || HasSerialTask(); });
            idle_--;

            if (!running_) {
                return;
            }

            // Process normal tasks first
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            } else {
                // Process serial tasks
                task = GetNextSerialTask();
            }
        }

        if (task) {
            auto fn = task->fn;
            if (fn) {
                try {
                    fn();
                } catch (const std::exception &e) {
                    NETWORK_LOGE("Task execution failed: %s", e.what());
                } catch (...) {
                    NETWORK_LOGE("Task execution failed with unknown exception");
                }
            }

            // If it's a serial task, clean up state after completion
            if (!task->tag.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                runningSerialTags_.erase(task->tag);
                // Add tag back to available queue if there are pending tasks
                if (serialTasks_.count(task->tag) && !serialTasks_[task->tag].empty()) {
                    availableSerialTags_.push(task->tag);
                    signal_.notify_one();
                }
            }

            // Return TaskItem to object pool
            ReleaseTaskItem(task);
        }
    }
}

void ThreadPool::AddTask(const Task &task, const std::string &serialTag)
{
    if (task == nullptr) {
        NETWORK_LOGE("task is nullptr");
        return;
    }

    auto t = AcquireTaskItem();
    t->reset(task, serialTag);
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            NETWORK_LOGE("ThreadPool is shutting down, task rejected");
            return;
        }

        if (serialTag.empty()) {
            // Normal task
            tasks_.push(t);
        } else {
            // Serial task
            if (runningSerialTags_.count(serialTag)) {
                // A task with the same tag is running, add to waiting queue
                serialTasks_[serialTag].push(t);
                return;
            } else {
                // No task with the same tag is running, add to normal queue directly
                runningSerialTags_.insert(serialTag);
                tasks_.push(t);
            }
        }

        // Create threads dynamically
        if (idle_ == 0 && threads_.size() < threadsMax_) {
            CreateWorkerThread();
        }
    }

    signal_.notify_one();
}

bool ThreadPool::HasSerialTask() const
{
    // Use the optimized available tags queue for O(1) check
    return !availableSerialTags_.empty();
}

std::shared_ptr<ThreadPool::TaskItem> ThreadPool::GetNextSerialTask()
{
    // Use the optimized available tags queue for O(1) lookup
    if (!availableSerialTags_.empty()) {
        std::string tag = availableSerialTags_.front();
        availableSerialTags_.pop();

        if (serialTasks_.count(tag) && !serialTasks_[tag].empty() && !runningSerialTags_.count(tag)) {
            auto task = serialTasks_[tag].front();
            serialTasks_[tag].pop();
            runningSerialTags_.insert(tag);

            // If the queue is empty, clean up the map
            if (serialTasks_[tag].empty()) {
                serialTasks_.erase(tag);
            }

            return task;
        }
    }

    return nullptr;
}

size_t ThreadPool::GetQueueSize() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    size_t total = tasks_.size();
    for (const auto &pair : serialTasks_) {
        total += pair.second.size();
    }
    return total;
}

size_t ThreadPool::GetThreadCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return threads_.size();
}

void ThreadPool::CreateWorkerThread()
{
    size_t threadIndex = threads_.size();
    auto p = std::make_unique<std::thread>(&ThreadPool::Worker, this);
#ifndef _WIN32
    std::string threadName = threadName_ + "-" + std::to_string(threadIndex);
    pthread_setname_np(p->native_handle(), threadName.c_str());
#endif
    threads_.emplace_back(std::move(p));
    NETWORK_LOGD("Created new thread, total: %zu/%d", threads_.size(), threadsMax_);
}

std::shared_ptr<ThreadPool::TaskItem> ThreadPool::AcquireTaskItem()
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!taskItemPool_.empty()) {
        auto item = taskItemPool_.top();
        taskItemPool_.pop();
        return item;
    }

    // Create new item if pool is empty
    return std::make_shared<TaskItem>();
}

void ThreadPool::ReleaseTaskItem(std::shared_ptr<TaskItem> item)
{
    if (!item) {
        return;
    }

    // Clear the item for reuse
    item->clear();

    std::lock_guard<std::mutex> lock(mutex_);

    // Only return to pool if we haven't exceeded the max size
    if (taskItemPool_.size() < POOL_SIZE_MAX) {
        taskItemPool_.push(item);
    }
}
} // namespace lmshao::network