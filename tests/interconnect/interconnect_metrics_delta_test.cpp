#include <iostream>
#include <string>

#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"
#include "interconnect/system_metrics_aggregator.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestSnapshotAndDelta() {
    vr::interconnect::SystemMetricsAggregator agg;

    vr::interconnect::BridgeMetrics bm1;
    bm1.tx_count = 10;
    bm1.rx_count = 8;
    bm1.route_miss_count = 1;
    agg.UpdateBridgeMetrics(bm1);

    vr::core::ThreadPoolMetrics tp1;
    tp1.executed_count = 20;
    tp1.rejected_count = 2;
    tp1.task_exception_count = 1;
    agg.UpdateThreadPoolMetrics(tp1);

    const auto s1 = agg.CaptureSnapshot();

    vr::interconnect::BridgeMetrics bm2 = bm1;
    bm2.tx_count += 5;
    bm2.rx_count += 4;
    bm2.route_miss_count += 2;
    agg.UpdateBridgeMetrics(bm2);

    vr::core::ThreadPoolMetrics tp2 = tp1;
    tp2.executed_count += 7;
    tp2.rejected_count += 1;
    tp2.task_exception_count += 2;
    agg.UpdateThreadPoolMetrics(tp2);

    const auto d1 = agg.ExportDeltaSinceLastCall();
    const auto d2 = agg.ExportDeltaSinceLastCall();

    return ExpectTrue(s1.timestamp_ms > 0U, "snapshot timestamp should be valid") &&
           ExpectTrue(d1.bridge_metrics_delta.tx_count == bm2.tx_count,
                      "first delta should export full tx on initial call") &&
           ExpectTrue(d1.thread_pool_executed_delta == tp2.executed_count,
                      "first delta should export full executed on initial call") &&
           ExpectTrue(d2.bridge_metrics_delta.tx_count == 0U,
                      "second delta should be zero without updates") &&
           ExpectTrue(d2.thread_pool_exception_delta == 0U,
                      "second delta exception should be zero");
}

}  // namespace

int main() {
    const bool ok = TestSnapshotAndDelta();
    if (!ok) {
        std::cerr << "interconnect metrics delta test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect metrics delta test passed." << std::endl;
    return 0;
}

