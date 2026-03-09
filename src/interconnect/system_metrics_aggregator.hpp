#ifndef VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP
#define VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP

#include <chrono>
#include <mutex>

#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"

namespace vr {
namespace interconnect {

struct MetricsSnapshot {
    BridgeMetrics bridge_metrics;
    vr::core::ThreadPoolMetrics thread_pool_metrics;
    std::uint64_t timestamp_ms{0U};
};

struct MetricsDelta {
    BridgeMetrics bridge_metrics_delta;
    std::uint64_t thread_pool_executed_delta{0U};
    std::uint64_t thread_pool_rejected_delta{0U};
    std::uint64_t thread_pool_exception_delta{0U};
    std::uint64_t from_timestamp_ms{0U};
    std::uint64_t to_timestamp_ms{0U};
};

class SystemMetricsAggregator {
public:
    void UpdateBridgeMetrics(const BridgeMetrics& metrics) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        bridge_metrics_ = metrics;
    }

    void UpdateThreadPoolMetrics(const vr::core::ThreadPoolMetrics& metrics) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_pool_metrics_ = metrics;
    }

    BridgeMetrics GetBridgeMetrics() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return bridge_metrics_;
    }

    vr::core::ThreadPoolMetrics GetThreadPoolMetrics() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return thread_pool_metrics_;
    }

    MetricsSnapshot CaptureSnapshot() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t now_ms = NowMs();
        return MetricsSnapshot{bridge_metrics_, thread_pool_metrics_, now_ms};
    }

    MetricsDelta ExportDeltaSinceLastCall() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t now_ms = NowMs();

        MetricsDelta delta;
        delta.bridge_metrics_delta.tx_count = bridge_metrics_.tx_count - last_export_bridge_.tx_count;
        delta.bridge_metrics_delta.tx_fail_count = bridge_metrics_.tx_fail_count - last_export_bridge_.tx_fail_count;
        delta.bridge_metrics_delta.rx_count = bridge_metrics_.rx_count - last_export_bridge_.rx_count;
        delta.bridge_metrics_delta.encode_fail_count =
            bridge_metrics_.encode_fail_count - last_export_bridge_.encode_fail_count;
        delta.bridge_metrics_delta.decode_fail_count =
            bridge_metrics_.decode_fail_count - last_export_bridge_.decode_fail_count;
        delta.bridge_metrics_delta.expired_drop_count =
            bridge_metrics_.expired_drop_count - last_export_bridge_.expired_drop_count;
        delta.bridge_metrics_delta.route_miss_count =
            bridge_metrics_.route_miss_count - last_export_bridge_.route_miss_count;
        delta.bridge_metrics_delta.invalid_envelope_count =
            bridge_metrics_.invalid_envelope_count - last_export_bridge_.invalid_envelope_count;
        delta.bridge_metrics_delta.transport_error_count =
            bridge_metrics_.transport_error_count - last_export_bridge_.transport_error_count;
        delta.bridge_metrics_delta.backpressure_drop_count =
            bridge_metrics_.backpressure_drop_count - last_export_bridge_.backpressure_drop_count;

        delta.thread_pool_executed_delta =
            thread_pool_metrics_.executed_count - last_export_thread_pool_.executed_count;
        delta.thread_pool_rejected_delta =
            thread_pool_metrics_.rejected_count - last_export_thread_pool_.rejected_count;
        delta.thread_pool_exception_delta =
            thread_pool_metrics_.task_exception_count - last_export_thread_pool_.task_exception_count;

        delta.from_timestamp_ms = last_export_timestamp_ms_;
        delta.to_timestamp_ms = now_ms;

        last_export_bridge_ = bridge_metrics_;
        last_export_thread_pool_ = thread_pool_metrics_;
        last_export_timestamp_ms_ = now_ms;

        return delta;
    }

private:
    static std::uint64_t NowMs() noexcept {
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        return static_cast<std::uint64_t>(now.time_since_epoch().count());
    }

    mutable std::mutex mutex_;
    BridgeMetrics bridge_metrics_{};
    vr::core::ThreadPoolMetrics thread_pool_metrics_{};

    BridgeMetrics last_export_bridge_{};
    vr::core::ThreadPoolMetrics last_export_thread_pool_{};
    std::uint64_t last_export_timestamp_ms_{0U};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP
