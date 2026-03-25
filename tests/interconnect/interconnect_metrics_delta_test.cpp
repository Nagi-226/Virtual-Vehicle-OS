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
    bm1.policy_lint_issue_count = 2;
    bm1.policy_lint_warning_count = 1;
    bm1.reload_success_count = 3;
    bm1.reload_fail_count = 1;
    bm1.reload_rollback_count = 1;
    bm1.last_reload_status_code = 42;
    bm1.trace_index_hit_count = 5;
    bm1.trace_index_miss_count = 2;
    bm1.trace_linked_event_count = 7;
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

    const auto json = agg.ExportJson();
    const auto lite = agg.ExportJsonLightweight();
    const auto prom = agg.ExportPrometheus();

    const bool has_lint = json.find("policy_lint_issue") != std::string::npos &&
        json.find("policy_lint_warning") != std::string::npos;
    const bool has_trace_index_json = json.find("trace_index_hit") != std::string::npos &&
        json.find("trace_index_miss") != std::string::npos &&
        json.find("trace_linked_event") != std::string::npos;
    const bool has_reload = json.find("reload_status") != std::string::npos;
    const bool has_lite_lint = lite.find("lint_i") != std::string::npos &&
        lite.find("lint_w") != std::string::npos;
    const bool has_trace_index_lite = lite.find("tidx_h") != std::string::npos &&
        lite.find("tidx_m") != std::string::npos &&
        lite.find("tlink") != std::string::npos;
    const bool has_lite_code = lite.find("r_code") != std::string::npos;
    const bool has_prom = prom.find("vv_policy_lint_issue_total") != std::string::npos &&
        prom.find("vv_policy_lint_warning_total") != std::string::npos &&
        prom.find("vv_reload_status_code") != std::string::npos;

    return ExpectTrue(s1.timestamp_ms > 0U, "snapshot timestamp should be valid") &&
           ExpectTrue(d1.bridge_metrics_delta.tx_count == bm2.tx_count,
                      "first delta should export full tx on initial call") &&
           ExpectTrue(d1.thread_pool_executed_delta == tp2.executed_count,
                      "first delta should export full executed on initial call") &&
           ExpectTrue(d2.bridge_metrics_delta.tx_count == 0U,
                      "second delta should be zero without updates") &&
           ExpectTrue(d2.thread_pool_exception_delta == 0U,
                      "second delta exception should be zero") &&
           ExpectTrue(has_lint, "json should include policy lint counts") &&
           ExpectTrue(has_reload, "json should include reload status") &&
           ExpectTrue(has_lite_lint, "lightweight json should include lint counts") &&
           ExpectTrue(has_lite_code, "lightweight json should include reload status") &&
           ExpectTrue(has_prom, "prometheus should include lint and reload status");
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

