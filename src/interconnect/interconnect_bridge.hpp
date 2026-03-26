#ifndef VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
#define VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/error_code.hpp"
#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"
#include "interconnect/bridge_policy.hpp"
#include "interconnect/config_provider.hpp"
#include "interconnect/diagnostics_manager.hpp"
#include "interconnect/diagnostics_reporter.hpp"
#include "interconnect/message_authenticator.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/message_router.hpp"
#include "interconnect/policy_manager.hpp"
#include "interconnect/protocol_manager.hpp"
#include "interconnect/system_metrics_aggregator.hpp"
#include "interconnect/transport.hpp"
#include "interconnect/transport_orchestrator.hpp"

namespace vr {
namespace interconnect {

enum class MessageProtocolMode : std::uint8_t {
    kLegacyPipe = 0,
    kCompact = 1,
    kProtobufReserved = 2,
    kCborReserved = 3,
};

struct BridgeConfig {
    TransportEndpointConfig vehicle_to_robot_endpoint;
    TransportEndpointConfig robot_to_vehicle_endpoint;
    std::vector<TransportEndpointConfig> additional_endpoints;
    vr::core::ThreadConfig thread_pool;
    BridgeSlaPolicy sla_policy;
    BridgePolicyTable policy_table;
    std::uint32_t receive_priority{0U};
    std::uint32_t trace_sample_rate_percent{10U};
    std::uint64_t trace_sample_interval_ms{2000U};
    std::uint64_t sla_violation_sample_interval_ms{5000U};
    std::uint64_t diagnostics_report_interval_ms{1000U};
    bool enable_diagnostics_reporting{false};
    bool enable_config_canary{false};
    std::uint32_t config_canary_percent{100U};
    std::string config_canary_topic_prefix;
    std::int32_t config_canary_channel{-1};
    std::string protocol_canary_topic_prefix;
    std::uint32_t protocol_canary_percent{100U};
    std::vector<std::string> idempotency_topics;
    std::uint32_t idempotency_window_size{128U};
    std::string diagnostics_snapshot_path{"build/diagnostics_snapshots.jsonl"};
    std::uint32_t diagnostics_snapshot_limit{100U};
    bool enable_config_risk_guard{false};
    bool auto_rollback_on_high_risk{true};
    std::uint32_t high_risk_block_threshold{3U};
    MessageProtocolMode protocol_mode{MessageProtocolMode::kLegacyPipe};
};

class InterconnectBridge {
public:
    InterconnectBridge(std::unique_ptr<ITransport> vehicle_to_robot_transport,
                       std::unique_ptr<ITransport> robot_to_vehicle_transport);
    ~InterconnectBridge();

    vr::core::ErrorCode Start(const BridgeConfig& config) noexcept;
    vr::core::ErrorCode Start(IConfigProvider* provider) noexcept;
    void Stop() noexcept;

    vr::core::ErrorCode PublishFromVehicle(const MessageEnvelope& envelope) noexcept;
    vr::core::ErrorCode PublishFromRobot(const MessageEnvelope& envelope) noexcept;

    MessageRouter& VehicleRouter() noexcept;
    MessageRouter& RobotRouter() noexcept;

    BridgeMetrics GetBridgeMetrics() const noexcept;
    vr::core::ThreadPoolMetrics GetThreadPoolMetrics() const noexcept;
    MetricsSnapshot CaptureMetricsSnapshot() noexcept;
    MetricsDelta ExportMetricsDelta() noexcept;
    std::string ExportPrometheusMetrics() const;
    std::string ExportJsonMetrics() const;
    std::string ExportJsonMetricsLightweight() const;
    std::string GetLoadedConfigSource() const;
    std::string GetPolicyLintReport() const;
    std::vector<std::string> DumpPolicyConflicts() const;
    std::string ExportPolicyEffectiveView() const;
    std::string DumpRuntimeState() const;
    std::string ExecuteDiagnosticCommand(const std::string& command) const;
    void SetDiagnosticsReporter(std::shared_ptr<IDiagnosticsReporter> reporter);
    void SetMessageAuthenticator(std::shared_ptr<IMessageAuthenticator> authenticator);
    std::string GetLastReloadAuditSummary() const;
    std::uint64_t GetLoadedConfigVersion() const noexcept;
    vr::core::ErrorCode ReloadConfigIfChanged(IConfigProvider* provider) noexcept;

private:
    vr::core::ErrorCode Publish(ITransport* transport, const MessageEnvelope& envelope) noexcept;
    vr::core::ErrorCode PublishWithBackpressure(ITransport* transport, const std::string& encoded,
                                                std::uint32_t priority,
                                                const BridgeSlaPolicy& policy) noexcept;
    const BridgeSlaPolicy& ResolvePolicy(const MessageEnvelope& envelope) const noexcept;

    void VehicleInboundLoop() noexcept;
    void RobotInboundLoop() noexcept;
    void ProcessInbound(ITransport* transport, MessageRouter* router,
                        const std::string& loop_name) noexcept;

    void RefreshAggregatedMetrics(bool force = false) noexcept;
    bool ShouldRefreshMetrics(const std::uint64_t now_ms) noexcept;
    void RebuildPolicyIndex();
    void PopulateDefaultTemplateRules();
    void ApplyTransportTuning(const BridgeConfig& config);
    std::uint32_t ResolvePriorityForPublish(const MessageEnvelope& envelope,
                                            const ITransport* transport) const noexcept;
    bool AcquireFlowSlot(const ITransport* transport) noexcept;
    void ReleaseFlowSlot(const ITransport* transport) noexcept;
    const BridgeSlaPolicy& ResolvePolicyInternal(const MessageEnvelope& envelope,
                                                 bool* out_override_applied,
                                                 bool* out_conflict_detected) const noexcept;
    bool RuleMatches(const PolicyRule& rule, const MessageEnvelope& envelope) const noexcept;
    bool RuleMatches(const PolicyRule& left, const PolicyRule& right) const noexcept;
    std::string FormatRuleKey(const PolicyRule& rule) const;
    std::string FormatRuleSummary(const PolicyRule& rule) const;
    std::vector<std::string> CollectPolicyConflicts() const;
    std::pair<std::uint64_t, std::uint64_t> GetPolicyLintCounts() const;
    std::string BuildPolicyLintReport() const;
    std::string BuildPolicyLintSummary() const;
    bool ShouldApplyCanaryForEnvelope(const MessageEnvelope& envelope) const;
    bool ShouldApplyIdempotency(const MessageEnvelope& envelope) const;
    bool IsDuplicateWithinWindow(const MessageEnvelope& envelope);
    const BridgeSlaPolicy* FindBestPolicyMatch(
        const std::vector<PolicyRule>& rules,
        const std::unordered_map<std::string, std::vector<std::size_t>>& index,
        const MessageEnvelope& envelope, std::uint32_t* out_priority,
        bool* out_conflict_detected) const noexcept;
    const PolicyCacheEntry* LookupPolicyCache(const MessageEnvelope& envelope) const noexcept;
    const PolicyCacheEntry* StorePolicyCache(const MessageEnvelope& envelope,
                                             const BridgeSlaPolicy& policy,
                                             bool override_applied,
                                             bool conflict_detected) const noexcept;
    static std::uint64_t NextLogAllowedMs(std::atomic<std::uint64_t>* timestamp,
                                         std::uint64_t now_ms,
                                         std::uint64_t interval_ms) noexcept;

    std::atomic<bool> running_{false};
    vr::core::ThreadPool worker_pool_;

    std::unique_ptr<ITransport> vehicle_to_robot_transport_;
    std::unique_ptr<ITransport> robot_to_vehicle_transport_;

    PolicyManager policy_manager_{};
    ProtocolManager protocol_manager_{};
    DiagnosticsManager diagnostics_manager_{};
    TransportOrchestrator transport_orchestrator_{};

    BridgeConfig config_{};
    MessageRouter vehicle_router_;
    MessageRouter robot_router_;
    std::uint32_t trace_sample_counter_{0U};
    std::uint32_t trace_sample_rate_percent_{kTraceIdSampleRatePercentDefault};
    std::uint64_t trace_sample_interval_ms_{kTraceIdSampleIntervalMsDefault};
    std::uint64_t sla_violation_sample_interval_ms_{kSlaViolationSampleIntervalMsDefault};

    std::atomic<std::uint64_t> tx_count_{0U};
    std::atomic<std::uint64_t> tx_fail_count_{0U};
    std::atomic<std::uint64_t> rx_count_{0U};
    std::atomic<std::uint64_t> encode_fail_count_{0U};
    std::atomic<std::uint64_t> decode_fail_count_{0U};
    std::atomic<std::uint64_t> expired_drop_count_{0U};
    std::atomic<std::uint64_t> route_miss_count_{0U};
    std::atomic<std::uint64_t> handler_error_count_{0U};
    std::atomic<std::uint64_t> invalid_envelope_count_{0U};
    std::atomic<std::uint64_t> transport_error_count_{0U};
    std::atomic<std::uint64_t> backpressure_drop_count_{0U};

    std::unordered_map<std::string, std::uint64_t> topic_tx_count_;
    std::unordered_map<std::string, std::uint64_t> topic_rx_count_;
    std::unordered_map<std::string, std::uint64_t> topic_expired_drop_count_;

    std::unordered_map<ChannelType, std::uint64_t> channel_tx_count_;
    std::unordered_map<ChannelType, std::uint64_t> channel_rx_count_;
    std::unordered_map<ChannelType, std::uint64_t> channel_expired_drop_count_;

    std::unordered_map<DeliveryQoS, std::uint64_t> qos_tx_count_;
    std::unordered_map<DeliveryQoS, std::uint64_t> qos_rx_count_;
    std::unordered_map<DeliveryQoS, std::uint64_t> qos_expired_drop_count_;

    mutable std::mutex metric_map_mutex_;

    std::atomic<std::uint64_t> policy_hit_count_{0U};
    std::atomic<std::uint64_t> policy_override_count_{0U};
    std::atomic<std::uint64_t> policy_conflict_count_{0U};
    std::atomic<std::uint64_t> policy_conflict_sampled_count_{0U};
    std::atomic<std::uint64_t> policy_cache_hit_count_{0U};
    std::atomic<std::uint64_t> policy_cache_miss_count_{0U};
    std::atomic<std::uint64_t> trace_id_present_count_{0U};
    std::atomic<std::uint64_t> trace_id_missing_count_{0U};
    std::atomic<std::uint64_t> trace_id_sampled_count_{0U};
    std::atomic<std::uint64_t> trace_index_hit_count_{0U};
    std::atomic<std::uint64_t> trace_index_miss_count_{0U};
    std::atomic<std::uint64_t> trace_linked_event_count_{0U};
    std::atomic<std::uint64_t> sla_violation_sampled_count_{0U};
    std::atomic<std::uint64_t> reload_success_count_{0U};
    std::atomic<std::uint64_t> reload_fail_count_{0U};
    std::atomic<std::uint64_t> reload_rollback_count_{0U};
    std::atomic<std::uint64_t> last_reload_timestamp_ms_{0U};
    std::atomic<std::int32_t> last_reload_status_code_{0};
    std::atomic<std::int32_t> reload_rollback_reason_code_{0};

    std::atomic<std::uint64_t> last_metrics_refresh_ms_{0U};
    std::atomic<std::uint64_t> last_receive_error_log_ms_{0U};
    std::atomic<std::uint64_t> last_decode_fail_log_ms_{0U};
    std::atomic<std::uint64_t> last_invalid_envelope_log_ms_{0U};
    std::atomic<std::uint64_t> last_expired_log_ms_{0U};
    std::atomic<std::uint64_t> last_trace_id_sample_ms_{0U};
    std::atomic<std::uint64_t> last_sla_violation_log_ms_{0U};
    std::atomic<std::uint64_t> last_route_miss_log_ms_{0U};
    std::atomic<std::uint64_t> last_handler_error_log_ms_{0U};
    std::atomic<std::uint64_t> last_policy_conflict_log_ms_{0U};

    static constexpr std::uint64_t kMetricsRefreshIntervalMs = 100U;
    static constexpr std::uint64_t kLogThrottleIntervalMs = 1000U;
    static constexpr std::uint64_t kPolicyConflictSampleIntervalMs = 5000U;
    static constexpr std::uint64_t kSlaViolationSampleIntervalMsDefault = 5000U;
    static constexpr std::uint64_t kTraceIdSampleIntervalMsDefault = 2000U;
    static constexpr std::uint32_t kTraceIdSampleRatePercentDefault = 10U;
    static constexpr std::uint64_t kPolicyCacheMaxEntries = 256U;

    struct PolicyCacheEntry {
        BridgeSlaPolicy policy;
        bool override_applied{false};
        bool conflict_detected{false};
    };

    struct PolicyCacheKey {
        std::string topic;
        ChannelType channel{ChannelType::kEvent};
        DeliveryQoS qos{DeliveryQoS::kBestEffort};

        bool operator==(const PolicyCacheKey& other) const {
            return topic == other.topic && channel == other.channel && qos == other.qos;
        }
    };

    struct PolicyCacheKeyHasher {
        std::size_t operator()(const PolicyCacheKey& key) const noexcept {
            const std::size_t h1 = std::hash<std::string>{}(key.topic);
            const std::size_t h2 = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.channel));
            const std::size_t h3 = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.qos));
            return h1 ^ (h2 << 1U) ^ (h3 << 2U);
        }
    };

    struct PolicyCacheItem {
        PolicyCacheKey key;
        PolicyCacheEntry entry;
    };

    mutable std::mutex policy_cache_mutex_;
    mutable std::list<PolicyCacheItem> policy_cache_items_;
    mutable std::unordered_map<PolicyCacheKey, std::list<PolicyCacheItem>::iterator,
                               PolicyCacheKeyHasher>
        policy_cache_index_;

    mutable std::mutex idempotency_mutex_;
    std::unordered_map<std::string, std::deque<std::string>> idempotency_recent_keys_;

    mutable std::mutex trace_index_mutex_;
    std::unordered_map<std::string, std::uint64_t> trace_index_last_seen_ms_;
    std::deque<std::string> trace_index_insert_order_;
    static constexpr std::size_t kTraceIndexMaxEntries = 1024U;

    std::uint64_t loaded_config_version_{0U};
    std::string loaded_config_source_;
    BridgeConfig last_known_good_config_{};
    std::uint64_t last_known_good_version_{0U};
    std::string last_known_good_source_;
    std::uint64_t last_known_good_signature_{0U};
    std::uint64_t last_loaded_signature_{0U};
    std::string last_reload_audit_summary_{};
    std::shared_ptr<IDiagnosticsReporter> diagnostics_reporter_{};
    std::shared_ptr<IMessageAuthenticator> authenticator_{};
    mutable std::mutex policy_lint_mutex_;
    mutable bool policy_lint_cache_valid_{false};
    mutable std::string policy_lint_report_;
    mutable std::uint64_t policy_lint_issue_count_{0U};
    mutable std::uint64_t policy_lint_warning_count_{0U};
    std::uint64_t diagnostics_report_interval_ms_{1000U};
    std::uint64_t last_diagnostics_report_ms_{0U};
    bool enable_diagnostics_reporting_{false};

    std::uint32_t flow_limit_vehicle_{256U};
    std::uint32_t flow_limit_robot_{256U};
    std::uint32_t high_priority_threshold_vehicle_{1U};
    std::uint32_t high_priority_threshold_robot_{1U};
    bool failover_from_vehicle_to_robot_enabled_{false};
    bool failover_from_robot_to_vehicle_enabled_{false};
    MessageProtocolMode protocol_mode_{MessageProtocolMode::kLegacyPipe};
    std::atomic<std::uint32_t> inflight_vehicle_{0U};
    std::atomic<std::uint32_t> inflight_robot_{0U};
    std::atomic<std::uint64_t> failover_hit_count_{0U};
    std::atomic<std::uint64_t> idempotency_drop_count_{0U};
    std::atomic<std::uint64_t> diagnostic_snapshot_write_count_{0U};
    std::atomic<std::uint64_t> diag_dump_state_count_{0U};
    std::atomic<std::uint64_t> diag_route_event_count_{0U};
    std::atomic<std::uint64_t> diag_failover_event_count_{0U};
    std::atomic<std::uint64_t> transport_primary_healthy_{1U};
    std::atomic<std::uint64_t> transport_secondary_healthy_{1U};

    mutable SystemMetricsAggregator metrics_aggregator_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
