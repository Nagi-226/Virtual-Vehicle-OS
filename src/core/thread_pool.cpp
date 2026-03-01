#include "core/thread_pool.hpp"

#include <pthread.h>

namespace vr {
namespace core {

ThreadPool::~ThreadPool() {
    Stop();
}

ErrorCode ThreadPool::Start(const ThreadConfig& config) {
    if (config.worker_count == 0U) {
        return ErrorCode::kInvalidParam;
    }
    if (running_) {
        return ErrorCode::kOk;
    }

    enable_realtime_ = config.enable_realtime;
    realtime_priority_ = config.realtime_priority;
    running_ = true;

    workers_.reserve(config.worker_count);
    for (std::size_t i = 0; i < config.worker_count; ++i) {
        workers_.emplace_back(&ThreadPool::WorkerLoop, this, i);

        if (enable_realtime_) {
            const ErrorCode ec = ConfigureRealtimePriority(workers_.back());
            if (ec != ErrorCode::kOk) {
                Stop();
                return ec;
            }
        }
    }

    return ErrorCode::kOk;
}

void ThreadPool::Stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }

    tasks_cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    std::queue<std::function<void()>> empty;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.swap(empty);
    }
}

ErrorCode ThreadPool::Enqueue(const std::function<void()>& task) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!running_) {
            return ErrorCode::kThreadStartFailed;
        }
        tasks_.push(task);
    }

    tasks_cv_.notify_one();
    return ErrorCode::kOk;
}

void ThreadPool::WorkerLoop(const std::size_t /*index*/) {
    for (;;) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this]() { return !running_ || !tasks_.empty(); });

            if (!running_ && tasks_.empty()) {
                return;
            }

            task = tasks_.front();
            tasks_.pop();
        }

        task();
    }
}

ErrorCode ThreadPool::ConfigureRealtimePriority(std::thread& worker) noexcept {
    sched_param sched{};
    sched.sched_priority = realtime_priority_;

    const int rc = pthread_setschedparam(worker.native_handle(), SCHED_FIFO, &sched);
    if (rc != 0) {
        return ErrorCode::kThreadPrioritySetFailed;
    }

    return ErrorCode::kOk;
}

}  // namespace core
}  // namespace vr

