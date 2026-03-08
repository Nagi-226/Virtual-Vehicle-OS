#ifndef VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP
#define VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP

#include <mutex>

#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"

namespace vr {
namespace interconnect {

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

private:
    mutable std::mutex mutex_;
    BridgeMetrics bridge_metrics_{};
    vr::core::ThreadPoolMetrics thread_pool_metrics_{};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP
