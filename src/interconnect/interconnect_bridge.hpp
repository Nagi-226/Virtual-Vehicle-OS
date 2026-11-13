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
#include "interconnect/bridge_config.hpp"
#include "interconnect/bridge_metrics.hpp"
#include "interconnect/bridge_policy.hpp"
#include "interconnect/config_provider.hpp"
#include "interconnect/diagnostics_manager.hpp"
#include "interconnect/diagnostics_reporter.hpp"
#include "interconnect/message_authenticator.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/message_router.hpp"
#include "interconnect/policy_cache.hpp"
#include "interconnect/policy_manager.hpp"
#include "interconnect/protocol_manager.hpp"
#include "interconnect/system_metrics_aggregator.hpp"
#include "interconnect/transport.hpp"
#include "interconnect/transport_orchestrator.hpp"

// 本头文件只做 InterconnectBridge 的类定义，不再兼作类型仓库：
//   MessageProtocolMode / BridgeConfig      -> interconnect/bridge_config.hpp
//   PolicyCacheEntry / PolicyCacheKey 等    -> interconnect/policy_cache.hpp
// 依赖方向（无环）：message_envelope -> transport -> bridge_policy
//   -> bridge_config -> 各 manager 头 -> interconnect_bridge.hpp

namespace vr {
namespace interconnect {

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

    // ======================= 指标读取入口 · 新鲜度语义 =======================
    // 五个入口的新鲜度**各不相同**，散在各自注释里读不全，所以集中列一张表。
    // 刷新间隔来源：kMetricsRefreshIntervalMs = 100U（本文件下方成员区）。
    //
    // | 入口                                | 返回                          | 新鲜度                    | 备注 |
    // |-------------------------------------|-------------------------------|---------------------------|------|
    // | GetBridgeMetrics()                  | BridgeMetrics                 | ❌ 缓存，滞后 ≤100ms      | 不触发刷新 |
    // | CaptureMetricsSnapshot()            | MetricsSnapshot（.bridge_metrics）| ✅ 强刷               | 要"刚发生必可见"用这个 |
    // | ExportPrometheusMetrics()           | std::string                   | ✅ 强刷                   | 每次 = 完整聚合（9 张 map 深拷贝），别进高频循环 |
    // | ExportJsonMetrics()                 | std::string                   | ✅ 强刷                   | 同上 |
    // | ExportJsonMetricsLightweight()      | std::string（字段更少）        | ❌ 缓存，滞后 ≤100ms      | **「轻」= 输出字段更少（是全量 JSON 的子集，省掉 idempotency_drop / backpressure_drop / reload 等一批计数器），不指更新鲜** |
    // | ExportMetricsDelta()                | MetricsDelta                  | ✅ 强刷                   | 本来就是（现成先例） |
    //
    // 判据/踩坑记录：发布或丢弃动作之后**立刻**读，只有带 ✅ 的三个入口能保证看到；
    // 用 ❌ 的两个读会拿到 100ms 窗口内的旧值（曾经让两条测试误报失败，见 docs/backlog.md BL-02）。
    // ======================================================================
    BridgeMetrics GetBridgeMetrics() const noexcept;
    vr::core::ThreadPoolMetrics GetThreadPoolMetrics() const noexcept;
    MetricsSnapshot CaptureMetricsSnapshot() noexcept;
    MetricsDelta ExportMetricsDelta() noexcept;
    // ↓ 这两个**进入时强刷**，保证导出即最新；代价是每次一次完整聚合。
    std::string ExportPrometheusMetrics() const;
    std::string ExportJsonMetrics() const;
    // ↓ 这个**故意不强刷**（轻 = 输出字段少，不是更新鲜），实现处有详细说明。
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

    // 刷新聚合指标。**声明为 const**：它确实"改状态"，但改的全是 mutable 成员
    // （last_metrics_refresh_ms_ 与 metrics_aggregator_），这样 const 的导出接口
    // （ExportPrometheusMetrics / ExportJsonMetrics）也能触发强刷 —— 用 mutable 而不是
    // const_cast：const_cast 会把"在 const 方法里改状态"藏起来，是隐式行为的温床。
    void RefreshAggregatedMetrics(bool force = false) const noexcept;
    bool ShouldRefreshMetrics(const std::uint64_t now_ms) const noexcept;
    void RebuildPolicyIndex();
    // 把默认控制/遥测模板规则填进给定策略表（表里已有模板规则时不动）。
    // 做成接受目标的静态函数而不是只操作 config_：reload 路径必须能在**落盘前**
    // 对候选配置做同样的填充，否则算出的签名与"实际生效的表"不是同一状态。
    static void PopulateDefaultTemplateRules(BridgePolicyTable* table);
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

    // 以下计数器均为**纯观测量**：const 的读路径（ResolvePolicy / DumpRuntimeState 等）
    // 也会自增它们。它们不参与任何逻辑判定，因此声明为 mutable 以保持读路径的
    // const 语义（"读不改逻辑状态，只改统计计数"），这比去掉 const 侵入更小。
    mutable std::atomic<std::uint64_t> policy_hit_count_{0U};
    mutable std::atomic<std::uint64_t> policy_override_count_{0U};
    mutable std::atomic<std::uint64_t> policy_conflict_count_{0U};
    mutable std::atomic<std::uint64_t> policy_conflict_sampled_count_{0U};
    mutable std::atomic<std::uint64_t> policy_cache_hit_count_{0U};
    mutable std::atomic<std::uint64_t> policy_cache_miss_count_{0U};
    mutable std::atomic<std::uint64_t> trace_id_present_count_{0U};
    mutable std::atomic<std::uint64_t> trace_id_missing_count_{0U};
    mutable std::atomic<std::uint64_t> trace_id_sampled_count_{0U};
    mutable std::atomic<std::uint64_t> trace_index_hit_count_{0U};
    mutable std::atomic<std::uint64_t> trace_index_miss_count_{0U};
    mutable std::atomic<std::uint64_t> trace_linked_event_count_{0U};
    mutable std::atomic<std::uint64_t> sla_violation_sampled_count_{0U};
    std::atomic<std::uint64_t> reload_success_count_{0U};
    std::atomic<std::uint64_t> reload_fail_count_{0U};
    std::atomic<std::uint64_t> reload_rollback_count_{0U};
    std::atomic<std::uint64_t> last_reload_timestamp_ms_{0U};
    std::atomic<std::int32_t> last_reload_status_code_{0};
    std::atomic<std::int32_t> reload_rollback_reason_code_{0};

    // ↓ 这一组是**节流用的时间戳**，mutable 的理由只有一个：
    //   const 的读路径/观测路径也要推进它们（刷新节流、日志节流、采样间隔），
    //   属于"逻辑 const、物理需更新缓存"，所以标 mutable 而不是去掉 const。
    //
    // ⚠️ 这个 mutable 不是"免费属性"：它是为**上述具体用途**加的，不是通用许可。
    //   尤其 `last_metrics_refresh_ms_` —— 目前 `ExportPrometheusMetrics()` /
    //   `ExportJsonMetrics()`（都是 const）依赖它可由 `RefreshAggregatedMetrics(true)`
    //   推进，才能做到"导出即最新"。**若有人把它改成非 mutable，这两个 const 导出会
    //   编译不过**；届时请恢复 mutable 或改成别的正确做法，
    //   **不要改用 `const_cast` 把"在 const 方法里改状态"藏起来**（见
    //   `RefreshAggregatedMetrics` 声明处的说明）。
    mutable std::atomic<std::uint64_t> last_metrics_refresh_ms_{0U};
    mutable std::atomic<std::uint64_t> last_receive_error_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_decode_fail_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_invalid_envelope_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_expired_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_trace_id_sample_ms_{0U};
    mutable std::atomic<std::uint64_t> last_sla_violation_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_route_miss_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_handler_error_log_ms_{0U};
    mutable std::atomic<std::uint64_t> last_policy_conflict_log_ms_{0U};

    static constexpr std::uint64_t kMetricsRefreshIntervalMs = 100U;
    static constexpr std::uint64_t kLogThrottleIntervalMs = 1000U;
    static constexpr std::uint64_t kPolicyConflictSampleIntervalMs = 5000U;
    static constexpr std::uint64_t kSlaViolationSampleIntervalMsDefault = 5000U;
    static constexpr std::uint64_t kTraceIdSampleIntervalMsDefault = 2000U;
    static constexpr std::uint32_t kTraceIdSampleRatePercentDefault = 10U;
    static constexpr std::uint64_t kPolicyCacheMaxEntries = 256U;

    // 策略解析缓存（类型见 interconnect/policy_cache.hpp）
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
    // 是否已经有过一次成功提交的配置（Start 成功或 reload 成功提交）。
    // 首次启动时不存在"上一份已知良好配置"，canary 灰度回退没有回退目标，
    // 必须放行；否则 config_canary_percent < 100 的配置会让设备永远起不来。
    bool has_committed_config_{false};
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
    mutable std::atomic<std::uint64_t> diag_dump_state_count_{0U};
    mutable std::atomic<std::uint64_t> diag_route_event_count_{0U};
    mutable std::atomic<std::uint64_t> diag_failover_event_count_{0U};
    std::atomic<std::uint64_t> transport_primary_healthy_{1U};
    std::atomic<std::uint64_t> transport_secondary_healthy_{1U};

    // mutable 的具体理由：多个 **const** 出口需要它"刷新缓存"才能给出一致观测 ——
    //   GetBridgeMetrics()（const，读缓存）、CaptureMetricsSnapshot()（非 const，强刷）、
    //   ExportPrometheusMetrics() / ExportJsonMetrics()（const，强刷）、
    //   ExportMetricsDelta()（非 const，强刷）。
    // 这些路径都会经由 `RefreshAggregatedMetrics()` 写入本成员；它是 const 方法可写的，
    // 唯一原因就是这里的 mutable。
    //
    // ⚠️ 不要把它当成"因为别处依赖所以顺手带上的属性"：如果将来重构搬走/去掉这个
    //   mutable，上面两个 const 导出会编译失败 —— 那时**不要**用 `const_cast` 绕过去
    //   （会把"在 const 里改状态"藏起来，正是本轮一直在消灭的隐式行为）。
    mutable SystemMetricsAggregator metrics_aggregator_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
