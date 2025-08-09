/**
 * @file task_queue.h
 * @brief Task Queue Header
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2024-2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NETWORK_TASK_QUEUE_H
#define NETWORK_TASK_QUEUE_H

#include <condition_variable>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

namespace lmshao::network {
class TaskQueue;
template <typename T>
class TaskHandler;

template <typename T>
struct TaskResult {
    bool HasResult() { return val.has_value(); }
    T Value() { return val.value(); }

private:
    friend class TaskHandler<T>;
    std::optional<T> val;
};

template <>
struct TaskResult<void> {
    bool HasResult() { return executed; }

private:
    friend class TaskHandler<void>;
    bool executed = false;
};

class ITaskHandler {
public:
    struct Attribute {
        explicit Attribute(uint64_t interval = UINT64_MAX) : periodicTimeUs_(interval) {}

        // periodic execute time, UINT64_MAX is not need to execute periodic.
        uint64_t periodicTimeUs_;
    };
    virtual ~ITaskHandler() = default;
    virtual void Execute() = 0;
    virtual void Cancel() = 0;
    virtual bool IsCanceled() = 0;
    virtual Attribute GetAttribute() const = 0;

private:
    // clear the internel executed or canceled state.
    virtual void Clear() = 0;
    friend class TaskQueue;
};

template <typename T>
class TaskHandler : public ITaskHandler {
public:
    explicit TaskHandler(std::function<T(void)> task, uint64_t interval = UINT64_MAX)
        : task_(task), attribute_(interval)
    {
    }

    ~TaskHandler() override = default;

    void Execute() override
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (state_ != TaskState::IDLE) {
                return;
            }
            state_ = TaskState::RUNNING;
        }

        if constexpr (std::is_void_v<T>) {
            task_();
            std::unique_lock<std::mutex> lock(mutex_);
            state_ = TaskState::FINISHED;
            result_.executed = true;
        } else {
            T result = task_();
            std::unique_lock<std::mutex> lock(mutex_);
            state_ = TaskState::FINISHED;
            result_.val = result;
        }
        cond_.notify_all();
    }

    // After the GetResult called, the last execute result will be clear
    TaskResult<T> GetResult()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        while ((state_ != TaskState::FINISHED) && (state_ != TaskState::CANCELED)) {
            cond_.wait(lock);
        }

        return ClearResult();
    }

    void Cancel() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (state_ != RUNNING) {
            state_ = TaskState::CANCELED;
            cond_.notify_all();
        }
    }

    bool IsCanceled() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return state_ == TaskState::CANCELED;
    }

    ITaskHandler::Attribute GetAttribute() const override { return attribute_; }

private:
    TaskResult<T> ClearResult()
    {
        if (state_ == TaskState::FINISHED) {
            state_ = TaskState::IDLE;
            TaskResult<T> tmp;
            if constexpr (std::is_void_v<T>) {
                std::swap(tmp.executed, result_.executed);
            } else {
                result_.val.swap(tmp.val);
            }
            return tmp;
        }
        return result_;
    }

    void Clear() override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        (void)ClearResult();
    }

    enum TaskState {
        IDLE = 0,
        RUNNING = 1,
        CANCELED = 2,
        FINISHED = 3,
    };

    TaskState state_ = TaskState::IDLE;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::function<T(void)> task_;
    TaskResult<T> result_;
    ITaskHandler::Attribute attribute_; // task execute attribute.
};

class TaskQueue {
public:
    explicit TaskQueue(const std::string &name) : name_(name) {}
    ~TaskQueue();

    int32_t Start();
    int32_t Stop() noexcept;

    bool IsTaskExecuting();

    // delayUs cannot be gt 10000000ULL.
    int32_t EnqueueTask(const std::shared_ptr<ITaskHandler> &task, bool cancelNotExecuted = false,
                        uint64_t delayUs = 0ULL);

private:
    struct TaskHandlerItem {
        std::shared_ptr<ITaskHandler> task_{nullptr};
        uint64_t executeTimeNs_{0ULL};
    };
    void TaskProcessor();
    void CancelNotExecutedTaskLocked();

    bool isExit_ = true;
    std::unique_ptr<std::thread> thread_;
    std::list<TaskHandlerItem> taskList_;
    std::mutex mutex_;
    std::condition_variable cond_;
    std::string name_;
    pid_t tid_ = -1;
    bool isTaskExecuting_ = false;
};

} // namespace lmshao::network

#endif // NETWORK_TASK_QUEUE_H
