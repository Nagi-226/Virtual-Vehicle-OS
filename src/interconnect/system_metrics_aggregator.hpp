#ifndef VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP
#define VR_INTERCONNECT_SYSTEM_METRICS_AGGREGATOR_HPP

#include <chrono>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

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
    std::string ExportPrometheus() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        const std::uint64_t ts = NowMs();

        auto emit_counter = [&oss, ts](const std::string& name, std::uint64_t value,
                                       const std::string& labels) {
            oss << name << labels << " " << value << " " << ts << "\n";
        };

        const std::string default_labels = "{topic=\"*\",channel=\"all\",qos=\"all\",endpoint=\"bridge\"}";

        emit_counter("vv_bridge_tx_total", bridge_metrics_.tx_count, default_labels);
        emit_counter("vv_bridge_tx_fail_total", bridge_metrics_.tx_fail_count, default_labels);
        emit_counter("vv_bridge_rx_total", bridge_metrics_.rx_count, default_labels);
        emit_counter("vv_bridge_encode_fail_total", bridge_metrics_.encode_fail_count, default_labels);
        emit_counter("vv_bridge_decode_fail_total", bridge_metrics_.decode_fail_count, default_labels);
        emit_counter("vv_bridge_expired_drop_total", bridge_metrics_.expired_drop_count, default_labels);
        emit_counter("vv_bridge_route_miss_total", bridge_metrics_.route_miss_count, default_labels);
        emit_counter("vv_bridge_handler_error_total", bridge_metrics_.handler_error_count, default_labels);
        emit_counter("vv_bridge_invalid_envelope_total", bridge_metrics_.invalid_envelope_count, default_labels);
        emit_counter("vv_bridge_transport_error_total", bridge_metrics_.transport_error_count, default_labels);
        emit_counter("vv_bridge_backpressure_drop_total", bridge_metrics_.backpressure_drop_count, default_labels);

        auto emit_by_topic = [&oss, ts](const std::string& name,
                                        const std::unordered_map<std::string, std::uint64_t>& bucket,
                                        const std::string& endpoint) {
            for (const auto& [topic, value] : bucket) {
                oss << name << "{topic=\"" << topic << "\",channel=\"all\",qos=\"all\",endpoint=\""
                    << endpoint << "\"} " << value << " " << ts << "\n";
            }
        };

        auto channel_to_label = [](ChannelType channel) {
            switch (channel) {
                case ChannelType::kControl:
                    return "control";
                case ChannelType::kTelemetry:
                    return "telemetry";
                case ChannelType::kEvent:
                default:
                    return "event";
            }
        };

        auto qos_to_label = [](DeliveryQoS qos) {
            switch (qos) {
                case DeliveryQoS::kAtLeastOnce:
                    return "at_least_once";
                case DeliveryQoS::kBestEffort:
                default:
                    return "best_effort";
            }
        };

        auto emit_by_channel = [&oss, ts, &channel_to_label](
            const std::string& name,
            const std::unordered_map<ChannelType, std::uint64_t>& bucket,
            const std::string& endpoint) {
            for (const auto& [channel, value] : bucket) {
                oss << name << "{topic=\"*\",channel=\"" << channel_to_label(channel)
                    << "\",qos=\"all\",endpoint=\"" << endpoint << "\"} " << value
                    << " " << ts << "\n";
            }
        };

        auto emit_by_qos = [&oss, ts, &qos_to_label](
            const std::string& name,
            const std::unordered_map<DeliveryQoS, std::uint64_t>& bucket,
            const std::string& endpoint) {
            for (const auto& [qos, value] : bucket) {
                oss << name << "{topic=\"*\",channel=\"all\",qos=\"" << qos_to_label(qos)
                    << "\",endpoint=\"" << endpoint << "\"} " << value << " " << ts << "\n";
            }
        };

        emit_by_topic("vv_bridge_tx_total", bridge_metrics_.topic_tx_count, "bridge");
        emit_by_topic("vv_bridge_rx_total", bridge_metrics_.topic_rx_count, "bridge");
        emit_by_topic("vv_bridge_expired_drop_total", bridge_metrics_.topic_expired_drop_count,
                      "bridge");
        emit_by_channel("vv_bridge_tx_total", bridge_metrics_.channel_tx_count, "bridge");
        emit_by_channel("vv_bridge_rx_total", bridge_metrics_.channel_rx_count, "bridge");
        emit_by_channel("vv_bridge_expired_drop_total", bridge_metrics_.channel_expired_drop_count,
                        "bridge");
        emit_by_qos("vv_bridge_tx_total", bridge_metrics_.qos_tx_count, "bridge");
        emit_by_qos("vv_bridge_rx_total", bridge_metrics_.qos_rx_count, "bridge");
        emit_by_qos("vv_bridge_expired_drop_total", bridge_metrics_.qos_expired_drop_count,
                    "bridge");

        emit_counter("vv_policy_hit_total", bridge_metrics_.policy_hit_count, default_labels);
        emit_counter("vv_policy_override_total", bridge_metrics_.policy_override_count, default_labels);
        emit_counter("vv_policy_conflict_total", bridge_metrics_.policy_conflict_count, default_labels);
        emit_counter("vv_policy_conflict_sample_total", bridge_metrics_.policy_conflict_sampled_count,
                     default_labels);
        emit_counter("vv_policy_cache_hit_total", bridge_metrics_.policy_cache_hit_count, default_labels);
        emit_counter("vv_policy_cache_miss_total", bridge_metrics_.policy_cache_miss_count, default_labels);
        emit_counter("vv_trace_id_present_total", bridge_metrics_.trace_id_present_count, default_labels);
        emit_counter("vv_trace_id_missing_total", bridge_metrics_.trace_id_missing_count, default_labels);
        emit_counter("vv_trace_id_sample_total", bridge_metrics_.trace_id_sampled_count, default_labels);
        emit_counter("vv_sla_violation_sample_total", bridge_metrics_.sla_violation_sampled_count,
                     default_labels);

        emit_counter("vv_reload_success_total", bridge_metrics_.reload_success_count, default_labels);
        emit_counter("vv_reload_fail_total", bridge_metrics_.reload_fail_count, default_labels);

        oss << "vv_loaded_config_version" << default_labels << " " << bridge_metrics_.loaded_config_version
            << " " << ts << "\n";
        oss << "vv_last_reload_timestamp_ms" << default_labels << " "
            << bridge_metrics_.last_reload_timestamp_ms << " " << ts << "\n";

        return oss.str();
    }

    std::string ExportJson() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        const std::uint64_t ts = NowMs();

        oss << "{";
        oss << "\"timestamp_ms\":" << ts << ",";
        oss << "\"bridge\":{";
        oss << "\"tx\":" << bridge_metrics_.tx_count << ",";
        oss << "\"tx_fail\":" << bridge_metrics_.tx_fail_count << ",";
        oss << "\"rx\":" << bridge_metrics_.rx_count << ",";
        oss << "\"encode_fail\":" << bridge_metrics_.encode_fail_count << ",";
        oss << "\"decode_fail\":" << bridge_metrics_.decode_fail_count << ",";
        oss << "\"expired_drop\":" << bridge_metrics_.expired_drop_count << ",";
        oss << "\"route_miss\":" << bridge_metrics_.route_miss_count << ",";
        oss << "\"handler_error\":" << bridge_metrics_.handler_error_count << ",";
        oss << "\"invalid_envelope\":" << bridge_metrics_.invalid_envelope_count << ",";
        oss << "\"transport_error\":" << bridge_metrics_.transport_error_count << ",";
        oss << "\"backpressure_drop\":" << bridge_metrics_.backpressure_drop_count << ",";
        oss << "\"policy_hit\":" << bridge_metrics_.policy_hit_count << ",";
        oss << "\"policy_override\":" << bridge_metrics_.policy_override_count << ",";
        oss << "\"policy_conflict\":" << bridge_metrics_.policy_conflict_count << ",";
        oss << "\"policy_conflict_sampled\":" << bridge_metrics_.policy_conflict_sampled_count << ",";
        oss << "\"policy_cache_hit\":" << bridge_metrics_.policy_cache_hit_count << ",";
        oss << "\"policy_cache_miss\":" << bridge_metrics_.policy_cache_miss_count << ",";
        oss << "\"trace_id_present\":" << bridge_metrics_.trace_id_present_count << ",";
        oss << "\"trace_id_missing\":" << bridge_metrics_.trace_id_missing_count << ",";
        oss << "\"trace_id_sampled\":" << bridge_metrics_.trace_id_sampled_count << ",";
        oss << "\"sla_violation_sampled\":" << bridge_metrics_.sla_violation_sampled_count;
        oss << "},";
        oss << "\"endpoint\":\"bridge\",";
        oss << "\"config_version\":" << bridge_metrics_.loaded_config_version;
        oss << "}";

        return oss.str();
    }

    std::string ExportJsonLightweight() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        const std::uint64_t ts = NowMs();

        oss << "{";
        oss << "\"ts\":" << ts << ",";
        oss << "\"b\":{";
        oss << "\"tx\":" << bridge_metrics_.tx_count << ",";
        oss << "\"rx\":" << bridge_metrics_.rx_count << ",";
        oss << "\"txf\":" << bridge_metrics_.tx_fail_count << ",";
        oss << "\"exp\":" << bridge_metrics_.expired_drop_count << ",";
        oss << "\"route\":" << bridge_metrics_.route_miss_count << ",";
        oss << "\"herr\":" << bridge_metrics_.handler_error_count << ",";
        oss << "\"pol_hit\":" << bridge_metrics_.policy_hit_count << ",";
        oss << "\"pol_miss\":" << bridge_metrics_.policy_cache_miss_count << ",";
        oss << "\"tid_p\":" << bridge_metrics_.trace_id_present_count << ",";
        oss << "\"tid_m\":" << bridge_metrics_.trace_id_missing_count << ",";
        oss << "\"sla_s\":" << bridge_metrics_.sla_violation_sampled_count;
        oss << "}";
        oss << ",\"ep\":\"bridge\"";
        oss << ",\"ver\":" << bridge_metrics_.loaded_config_version;
        oss << "}";

        return oss.str();
    }

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
        delta.bridge_metrics_delta.handler_error_count =
            bridge_metrics_.handler_error_count - last_export_bridge_.handler_error_count;
        delta.bridge_metrics_delta.invalid_envelope_count =
            bridge_metrics_.invalid_envelope_count - last_export_bridge_.invalid_envelope_count;
        delta.bridge_metrics_delta.transport_error_count =
            bridge_metrics_.transport_error_count - last_export_bridge_.transport_error_count;
        delta.bridge_metrics_delta.backpressure_drop_count =
            bridge_metrics_.backpressure_drop_count - last_export_bridge_.backpressure_drop_count;
        delta.bridge_metrics_delta.policy_hit_count =
            bridge_metrics_.policy_hit_count - last_export_bridge_.policy_hit_count;
        delta.bridge_metrics_delta.policy_override_count =
            bridge_metrics_.policy_override_count - last_export_bridge_.policy_override_count;
        delta.bridge_metrics_delta.policy_conflict_count =
            bridge_metrics_.policy_conflict_count - last_export_bridge_.policy_conflict_count;
        delta.bridge_metrics_delta.policy_conflict_sampled_count =
            bridge_metrics_.policy_conflict_sampled_count -
            last_export_bridge_.policy_conflict_sampled_count;
        delta.bridge_metrics_delta.policy_cache_hit_count =
            bridge_metrics_.policy_cache_hit_count - last_export_bridge_.policy_cache_hit_count;
        delta.bridge_metrics_delta.policy_cache_miss_count =
            bridge_metrics_.policy_cache_miss_count - last_export_bridge_.policy_cache_miss_count;
        delta.bridge_metrics_delta.trace_id_present_count =
            bridge_metrics_.trace_id_present_count - last_export_bridge_.trace_id_present_count;
        delta.bridge_metrics_delta.trace_id_missing_count =
            bridge_metrics_.trace_id_missing_count - last_export_bridge_.trace_id_missing_count;
        delta.bridge_metrics_delta.trace_id_sampled_count =
            bridge_metrics_.trace_id_sampled_count - last_export_bridge_.trace_id_sampled_count;
        delta.bridge_metrics_delta.sla_violation_sampled_count =
            bridge_metrics_.sla_violation_sampled_count -
            last_export_bridge_.sla_violation_sampled_count;
        delta.bridge_metrics_delta.reload_success_count =
            bridge_metrics_.reload_success_count - last_export_bridge_.reload_success_count;
        delta.bridge_metrics_delta.reload_fail_count =
            bridge_metrics_.reload_fail_count - last_export_bridge_.reload_fail_count;

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
