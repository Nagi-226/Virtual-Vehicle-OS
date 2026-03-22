#include "core/thread_pool.hpp"

#include <exception>

#include "log/logger.hpp"

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace vr {
namespace core {

ThreadPool::~ThreadPool() {
    Stop();
}

// 启动线程池：校验配置 -> 初始化运行状态与指标 -> 创建工作线程。
// 设计意图：启动路径失败必须“可回滚”，避免半启动状态导致后续行为不确定。
ErrorCode ThreadPool::Start(const ThreadConfig& config) {
    if (config.worker_count == 0U || config.queue_capacity == 0U) {
        return ErrorCode::kInvalidParam;
    }
    if (running_.load(std::memory_order_acquire)) {
        return ErrorCode::kOk;
    }

    enable_realtime_ = config.enable_realtime;
    realtime_priority_ = config.realtime_priority;
    queue_capacity_ = config.queue_capacity;
    rejection_policy_ = config.rejection_policy;
    running_.store(true, std::memory_order_release);

    submitted_count_.store(0U, std::memory_order_relaxed);
    executed_count_.store(0U, std::memory_order_relaxed);
    rejected_count_.store(0U, std::memory_order_relaxed);
    task_exception_count_.store(0U, std::memory_order_relaxed);

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
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        running_.store(false, std::memory_order_release);
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

// 入队任务：当队列满时根据拒绝策略处理（拒绝/调用者执行）。
// 对初级同学提示：CallerRuns 会牺牲提交线程响应性，但可减少任务直接丢失。
ErrorCode ThreadPool::Enqueue(const std::function<void()>& task) {
    if (!task) {
        return ErrorCode::kInvalidParam;
    }

    bool run_in_caller = false;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        if (!running_.load(std::memory_order_acquire)) {
            return ErrorCode::kThreadStartFailed;
        }

        if (tasks_.size() >= queue_capacity_) {
            rejected_count_.fetch_add(1U, std::memory_order_relaxed);
            if (rejection_policy_ == RejectionPolicy::kCallerRuns) {
                run_in_caller = true;
            } else {
                return ErrorCode::kThreadQueueFull;
            }
        } else {
            tasks_.push(task);
            submitted_count_.fetch_add(1U, std::memory_order_relaxed);
        }
    }

    if (run_in_caller) {
        submitted_count_.fetch_add(1U, std::memory_order_relaxed);
        ExecuteTask(task);
        return ErrorCode::kThreadTaskRejected;
    }

    tasks_cv_.notify_one();
    return ErrorCode::kOk;
}

ThreadPoolMetrics ThreadPool::GetMetrics() const noexcept {
    ThreadPoolMetrics metrics;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        metrics.queue_size = tasks_.size();
        metrics.queue_capacity = queue_capacity_;
        metrics.worker_count = workers_.size();
        metrics.running = running_.load(std::memory_order_relaxed);
    }

    metrics.submitted_count = submitted_count_.load(std::memory_order_relaxed);
    metrics.executed_count = executed_count_.load(std::memory_order_relaxed);
    metrics.rejected_count = rejected_count_.load(std::memory_order_relaxed);
    metrics.task_exception_count = task_exception_count_.load(std::memory_order_relaxed);
    return metrics;
}

void ThreadPool::WorkerLoop(const std::size_t /*index*/) {
    for (;;) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            tasks_cv_.wait(lock, [this]() {
                return !running_.load(std::memory_order_acquire) || !tasks_.empty();
            });

            if (!running_.load(std::memory_order_acquire) && tasks_.empty()) {
                return;
            }

            task = tasks_.front();
            tasks_.pop();
        }

        ExecuteTask(task);
    }
}

ErrorCode ThreadPool::ConfigureRealtimePriority(std::thread& worker) noexcept {
#if defined(__linux__) || defined(__APPLE__)
    sched_param sched{};
    sched.sched_priority = realtime_priority_;

    const int rc = pthread_setschedparam(worker.native_handle(), SCHED_FIFO, &sched);
    if (rc != 0) {
        return ErrorCode::kThreadPrioritySetFailed;
    }

    return ErrorCode::kOk;
#else
    (void)worker;
    return ErrorCode::kThreadPrioritySetFailed;
#endif
}

void ThreadPool::ExecuteTask(const std::function<void()>& task) noexcept {
    try {
        task();
    } catch (const std::exception& ex) {
        task_exception_count_.fetch_add(1U, std::memory_order_relaxed);
        LOG_ERROR(std::string("ThreadPool task exception: ") + ex.what());
    } catch (...) {
        task_exception_count_.fetch_add(1U, std::memory_order_relaxed);
        LOG_ERROR("ThreadPool task unknown exception");
    }

    executed_count_.fetch_add(1U, std::memory_order_relaxed);
}

}  // namespace core
}  // namespace vr
