#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "core/thread_pool.hpp"

namespace {

using vr::core::ErrorCode;
using vr::core::RejectionPolicy;
using vr::core::ThreadConfig;
using vr::core::ThreadPool;

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestStartWithInvalidConfig() {
    ThreadPool pool;

    ThreadConfig config;
    config.worker_count = 0;
    config.queue_capacity = 8;

    const ErrorCode ec = pool.Start(config);
    return ExpectTrue(ec == ErrorCode::kInvalidParam, "Start should fail when worker_count is 0");
}

bool TestEnqueueBeforeStart() {
    ThreadPool pool;
    const ErrorCode ec = pool.Enqueue([]() {});
    return ExpectTrue(ec == ErrorCode::kThreadStartFailed, "Enqueue before Start should fail");
}

bool TestRejectPolicyRejectNewTask() {
    ThreadPool pool;

    ThreadConfig config;
    config.worker_count = 1;
    config.queue_capacity = 1;
    config.rejection_policy = RejectionPolicy::kRejectNewTask;

    if (!ExpectTrue(pool.Start(config) == ErrorCode::kOk, "Start failed in reject policy test")) {
        return false;
    }

    std::atomic<bool> gate{false};
    std::atomic<bool> worker_started{false};

    const ErrorCode first = pool.Enqueue([&gate, &worker_started]() {
        worker_started.store(true, std::memory_order_relaxed);
        while (!gate.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!worker_started.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const ErrorCode second = pool.Enqueue([]() {});
    const ErrorCode third = pool.Enqueue([]() {});

    gate.store(true, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto metrics = pool.GetMetrics();
    pool.Stop();

    return ExpectTrue(first == ErrorCode::kOk, "First enqueue should succeed") &&
           ExpectTrue(second == ErrorCode::kOk, "Second enqueue should fill queue") &&
           ExpectTrue(third == ErrorCode::kThreadQueueFull,
                      "Third enqueue should be rejected when queue is full") &&
           ExpectTrue(metrics.rejected_count >= 1U, "Rejected metrics should be updated");
}

bool TestRejectPolicyCallerRuns() {
    ThreadPool pool;

    ThreadConfig config;
    config.worker_count = 1;
    config.queue_capacity = 1;
    config.rejection_policy = RejectionPolicy::kCallerRuns;

    if (!ExpectTrue(pool.Start(config) == ErrorCode::kOk, "Start failed in caller-runs test")) {
        return false;
    }

    std::atomic<bool> gate{false};
    std::atomic<bool> worker_started{false};
    std::atomic<std::uint32_t> run_count{0U};

    const ErrorCode first = pool.Enqueue([&gate, &worker_started, &run_count]() {
        worker_started.store(true, std::memory_order_relaxed);
        ++run_count;
        while (!gate.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!worker_started.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const ErrorCode second = pool.Enqueue([&run_count]() { ++run_count; });
    const ErrorCode third = pool.Enqueue([&run_count]() { ++run_count; });

    gate.store(true, std::memory_order_relaxed);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const auto metrics = pool.GetMetrics();
    pool.Stop();

    return ExpectTrue(first == ErrorCode::kOk, "First enqueue should succeed") &&
           ExpectTrue(second == ErrorCode::kOk, "Second enqueue should fill queue") &&
           ExpectTrue(third == ErrorCode::kThreadTaskRejected,
                      "Caller-runs policy should return THREAD_TASK_REJECTED") &&
           ExpectTrue(run_count.load(std::memory_order_relaxed) >= 3U,
                      "All tasks should be executed under caller-runs policy") &&
           ExpectTrue(metrics.rejected_count >= 1U, "Rejected metrics should be updated") &&
           ExpectTrue(metrics.executed_count >= 3U, "Executed metrics should be updated");
}

bool TestStopAndMetrics() {
    ThreadPool pool;

    ThreadConfig config;
    config.worker_count = 2;
    config.queue_capacity = 8;

    if (!ExpectTrue(pool.Start(config) == ErrorCode::kOk, "Start failed in stop-metrics test")) {
        return false;
    }

    std::atomic<std::uint32_t> executed{0U};
    for (std::uint32_t i = 0; i < 4U; ++i) {
        const ErrorCode ec = pool.Enqueue([&executed]() { ++executed; });
        if (!ExpectTrue(ec == ErrorCode::kOk, "Enqueue should succeed in stop-metrics test")) {
            pool.Stop();
            return false;
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const auto before_stop = pool.GetMetrics();
    pool.Stop();
    const auto after_stop = pool.GetMetrics();

    const ErrorCode enqueue_after_stop = pool.Enqueue([]() {});

    return ExpectTrue(executed.load(std::memory_order_relaxed) == 4U, "All tasks should execute") &&
           ExpectTrue(before_stop.running, "Pool should report running before Stop") &&
           ExpectTrue(!after_stop.running, "Pool should report not running after Stop") &&
           ExpectTrue(enqueue_after_stop == ErrorCode::kThreadStartFailed,
                      "Enqueue after Stop should fail") &&
           ExpectTrue(after_stop.executed_count >= 4U, "Metrics executed_count should be >= 4");
}

}  // namespace

int main() {
    const bool ok = TestStartWithInvalidConfig() && TestEnqueueBeforeStart() &&
                    TestRejectPolicyRejectNewTask() && TestRejectPolicyCallerRuns() &&
                    TestStopAndMetrics();

    if (!ok) {
        std::cerr << "ThreadPool tests failed." << std::endl;
        return 1;
    }

    std::cout << "ThreadPool tests passed." << std::endl;
    return 0;
}
