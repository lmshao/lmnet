//
// Copyright © 2024-2025 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#ifndef NETWORK_THREAD_POOL_H
#define NETWORK_THREAD_POOL_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stack>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lmshao::network {
constexpr int THREAD_NUM_MAX = 2;
constexpr int THREAD_NUM_PRE_ALLOC = 1;

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(int preAlloc = THREAD_NUM_PRE_ALLOC, int threadsMax = THREAD_NUM_MAX, std::string name = "");
    ~ThreadPool();

    void AddTask(const Task &task, const std::string &serialTag = "");

    void Shutdown();
    size_t GetQueueSize() const;
    size_t GetThreadCount() const;

private:
    void Worker();
    void CreateWorkerThread();

    struct TaskItem {
        TaskItem() = default;
        TaskItem(Task task, std::string serialTag) : fn(std::move(task)), tag(std::move(serialTag)) {}

        // Reset for reuse
        void reset(Task task, std::string serialTag)
        {
            fn = std::move(task);
            tag = std::move(serialTag);
        }

        // Clear for return to pool
        void clear()
        {
            fn = nullptr;
            tag.clear();
        }

        Task fn;
        std::string tag;
    };

    bool HasSerialTask() const;
    std::shared_ptr<TaskItem> GetNextSerialTask();

    std::shared_ptr<TaskItem> AcquireTaskItem();
    void ReleaseTaskItem(std::shared_ptr<TaskItem> item);

private:
    std::atomic<bool> running_{true};
    std::atomic<bool> shutdown_{false};

    int threadsMax_;
    std::atomic<int> idle_{0};
    std::string threadName_ = "threadpool";

    mutable std::mutex mutex_;
    std::condition_variable signal_;

    std::queue<std::shared_ptr<TaskItem>> tasks_;
    std::vector<std::unique_ptr<std::thread>> threads_;

    std::unordered_map<std::string, std::queue<std::shared_ptr<TaskItem>>> serialTasks_;
    std::unordered_set<std::string> runningSerialTags_;

    // Optimization: Keep track of available serial tags for O(1) lookup
    std::queue<std::string> availableSerialTags_;

    std::stack<std::shared_ptr<TaskItem>> taskItemPool_;
};

} // namespace lmshao::network

#endif // NETWORK_THREAD_POOL_H