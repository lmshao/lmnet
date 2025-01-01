//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "thread_pool.h"

#include <string>
#include <utility>

#include "log.h"

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

    for (int i = 0; i < preAlloc; ++i) {
        auto p = std::make_unique<std::thread>(&ThreadPool::Worker, this);
        std::string threadName = threadName_ + "-" + std::to_string(i);
        pthread_setname_np(p->native_handle(), threadName.c_str());
        threads_.emplace(std::move(p));
    }
}

ThreadPool::~ThreadPool()
{
    running_ = false;
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
        while (tasks_.empty()) {
            std::unique_lock<std::mutex> taskLock(signalMutex_);
            idle_++;
            signal_.wait(taskLock);
            idle_--;
            if (!running_) {
                return;
            }
        }

        std::unique_lock<std::mutex> lock(taskMutex_);
        if (tasks_.empty()) {
            continue;
        }

        // Take out a task
        auto task = std::move(tasks_.front());
        tasks_.pop();
        lock.unlock();

        if (task) {
            auto fn = task->fn;
            if (fn) {
                fn();
            }
        }
    }
}

void ThreadPool::AddTask(const Task &task, const std::string &serialTag)
{
    if (task == nullptr) {
        NETWORK_LOGE("task is nullptr");
        return;
    }

    auto t = std::make_shared<TaskItem>(task, serialTag);
    std::unique_lock<std::mutex> lock(taskMutex_);
    tasks_.push(t);
    lock.unlock();

    if (idle_ > 0) {
        signal_.notify_one();
        return;
    }

    NETWORK_LOGD("idle:%d, thread num: %zu/%d", idle_.load(), threads_.size(), threadsMax_);
    if (threads_.size() < threadsMax_) {
        auto p = std::make_unique<std::thread>(&ThreadPool::Worker, this);
        std::string threadName = threadName_ + "-" + std::to_string(threads_.size());
        pthread_setname_np(p->native_handle(), threadName_.c_str());
        threads_.emplace(std::move(p));
    }
}
