#ifndef VR_CORE_THREAD_POOL_HPP
#define VR_CORE_THREAD_POOL_HPP

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

struct ThreadConfig {
    std::size_t worker_count{2U};
    bool enable_realtime{false};
    std::int32_t realtime_priority{10};
};

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool();

    ErrorCode Start(const ThreadConfig& config);
    void Stop() noexcept;
    ErrorCode Enqueue(const std::function<void()>& task);

private:
    void WorkerLoop(std::size_t index);
    ErrorCode ConfigureRealtimePriority(std::thread& worker) noexcept;

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex tasks_mutex_;
    std::condition_variable tasks_cv_;
    bool running_{false};
    bool enable_realtime_{false};
    std::int32_t realtime_priority_{10};
};

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_THREAD_POOL_HPP

