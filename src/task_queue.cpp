//
// Copyright © 2024 SHAO Liming <lmshao@163.com>. All rights reserved.
//

#include "task_queue.h"

#include <unistd.h>
#include "log.h"

TaskQueue::~TaskQueue()
{
    (void)Stop();
}

int32_t TaskQueue::Start()
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (thread_ != nullptr) {
        NETWORK_LOGE("Started already, ignore ! [%s]", name_.c_str());
        return 0;
    }

    isExit_ = false;
    thread_ = std::make_unique<std::thread>(&TaskQueue::TaskProcessor, this);
    return 0;
}

int32_t TaskQueue::Stop() noexcept
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (isExit_) {
        NETWORK_LOGE("Stopped already, ignore ! [%s]", name_.c_str());
        return 0;
    }

    if (std::this_thread::get_id() == thread_->get_id()) {
        NETWORK_LOGE("Stop at the task thread, reject");
        return -1;
    }

    std::unique_ptr<std::thread> t;
    isExit_ = true;
    cond_.notify_all();
    std::swap(thread_, t);
    lock.unlock();

    if (t != nullptr && t->joinable()) {
        t->join();
    }

    lock.lock();
    CancelNotExecutedTaskLocked();
    return 0;
}

// cancelNotExecuted = false, delayUs = 0ULL.
int32_t TaskQueue::EnqueueTask(const std::shared_ptr<ITaskHandler> &task, bool cancelNotExecuted, uint64_t delayUs)
{

    constexpr uint64_t MAX_DELAY_US = 10000000ULL; // max delay.

    if (task == nullptr) {
        NETWORK_LOGE("Enqueue task when taskqueue task is nullptr.[%s]\n", name_.c_str());
        return -1;
    }

    task->Clear();

    if (delayUs >= MAX_DELAY_US) {
        NETWORK_LOGE("Enqueue task when taskqueue delayUs[%ld] is >= max delayUs %ld invalid! [%s]", delayUs,
                     MAX_DELAY_US, name_.c_str());
        return -1;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (isExit_) {
        NETWORK_LOGE("Enqueue task when taskqueue is stopped, failed ! [%s]", name_.c_str());
        return -1;
    }

    if (cancelNotExecuted) {
        CancelNotExecutedTaskLocked();
    }

    // 1000 is ns to us.
    constexpr uint32_t US_TO_NS = 1000;
    uint64_t curTimeNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());

    if (curTimeNs >= UINT64_MAX - delayUs * US_TO_NS) {
        NETWORK_LOGE("Enqueue task but timestamp is overflow, why? [%s]", name_.c_str());
        return -1;
    }

    uint64_t executeTimeNs = delayUs * US_TO_NS + curTimeNs;
    auto iter = std::find_if(taskList_.begin(), taskList_.end(), [executeTimeNs](const TaskHandlerItem &item) {
        return (item.executeTimeNs_ > executeTimeNs);
    });

    (void)taskList_.insert(iter, {task, executeTimeNs});
    cond_.notify_all();

    return 0;
}

void TaskQueue::CancelNotExecutedTaskLocked()
{
    NETWORK_LOGE("All task not executed are being cancelled..........[%s]", name_.c_str());
    while (!taskList_.empty()) {
        std::shared_ptr<ITaskHandler> task = taskList_.front().task_;
        taskList_.pop_front();
        if (task != nullptr) {
            task->Cancel();
        }
    }
}

void TaskQueue::TaskProcessor()
{
    constexpr uint32_t nameSizeMax = 15;
    tid_ = gettid();
    NETWORK_LOGE("Enter TaskProcessor [%s], tid_: (%d)\n", name_.c_str(), tid_);
    fflush(stdout);

    pthread_setname_np(pthread_self(), name_.substr(0, nameSizeMax).c_str());

    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock, [this] { return isExit_ || !taskList_.empty(); });
        if (isExit_) {
            NETWORK_LOGE("Exit TaskProcessor [%s], tid_: (%d)\n", name_.c_str(), tid_);
            return;
        }
        TaskHandlerItem item = taskList_.front();
        uint64_t curTimeNs = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        if (curTimeNs >= item.executeTimeNs_) {
            taskList_.pop_front();
        } else {
            uint64_t diff = item.executeTimeNs_ - curTimeNs;
            (void)cond_.wait_for(lock, std::chrono::nanoseconds(diff));
            continue;
        }
        isTaskExecuting_ = true;
        lock.unlock();

        if (item.task_ == nullptr || item.task_->IsCanceled()) {
            NETWORK_LOGE("task is nullptr or task canceled. [%s]", name_.c_str());
            lock.lock();
            isTaskExecuting_ = false;
            lock.unlock();
            continue;
        }

        item.task_->Execute();
        lock.lock();
        isTaskExecuting_ = false;
        lock.unlock();
        if (item.task_->GetAttribute().periodicTimeUs_ == UINT64_MAX) {
            continue;
        }
        int32_t res = EnqueueTask(item.task_, false, item.task_->GetAttribute().periodicTimeUs_);
        if (res != 0) {
            NETWORK_LOGE("enqueue periodic task failed:%d, why? [%s]", res, name_.c_str());
        }
    }
    //    (void)mallopt(M_FLUSH_THREAD_CACHE, 0);
    NETWORK_LOGE("Leave TaskProcessor [%s]", name_.c_str());
}

bool TaskQueue::IsTaskExecuting()
{
    std::unique_lock<std::mutex> lock(mutex_);
    return isTaskExecuting_;
}