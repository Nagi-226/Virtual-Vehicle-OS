#ifndef VR_CORE_THREAD_POOL_HPP
#define VR_CORE_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "core/error_code.hpp"

namespace vr {
namespace core {

enum class RejectionPolicy {
    kRejectNewTask = 0,
    kCallerRuns = 1
};

struct ThreadConfig {
    std::size_t worker_count{2U};
    bool enable_realtime{false};
    std::int32_t realtime_priority{10};
    std::size_t queue_capacity{64U};
    RejectionPolicy rejection_policy{RejectionPolicy::kRejectNewTask};
};

struct ThreadPoolMetrics {
    std::size_t queue_size{0U};
    std::size_t queue_capacity{0U};
    std::uint64_t submitted_count{0U};
    std::uint64_t executed_count{0U};
    std::uint64_t rejected_count{0U};
    std::uint64_t task_exception_count{0U};
    std::size_t worker_count{0U};
    bool running{false};
};

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool();

    ErrorCode Start(const ThreadConfig& config);
    void Stop() noexcept;
    ErrorCode Enqueue(const std::function<void()>& task);
    ThreadPoolMetrics GetMetrics() const noexcept;

private:
    void WorkerLoop(std::size_t index);
    ErrorCode ConfigureRealtimePriority(std::thread& worker) noexcept;
    void ExecuteTask(const std::function<void()>& task) noexcept;

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    bool running_{false};
    bool enable_realtime_{false};
    std::int32_t realtime_priority_{10};
    std::size_t queue_capacity_{64U};
    RejectionPolicy rejection_policy_{RejectionPolicy::kRejectNewTask};

    std::atomic<std::uint64_t> submitted_count_{0U};
    std::atomic<std::uint64_t> executed_count_{0U};
    std::atomic<std::uint64_t> rejected_count_{0U};
    std::atomic<std::uint64_t> task_exception_count_{0U};
};

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_THREAD_POOL_HPP
