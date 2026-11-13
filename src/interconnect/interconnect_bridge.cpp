#include "interconnect/interconnect_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "core/retry_policy.hpp"
#include "interconnect/message_codec.hpp"
#include "interconnect/message_protocol_adapter.hpp"
#include "log/logger.hpp"

namespace vr {
namespace interconnect {

namespace {

std::uint64_t NowUnixMs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

//消息过期判断
bool IsExpired(const MessageEnvelope& envelope, const std::uint64_t now_ms,
               const std::uint32_t max_latency_ms) {
    const std::uint32_t effective_ttl =
        envelope.ttl_ms == 0U ? max_latency_ms : std::min(envelope.ttl_ms, max_latency_ms);

    if (effective_ttl == 0U) {
        return false;
    }
    if (now_ms < envelope.timestamp_ms) {
        return false;
    }
    return (now_ms - envelope.timestamp_ms) > effective_ttl;
}

// 是否允许发布消息
bool IsEnvelopeValidForPublish(const MessageEnvelope& envelope) {
    return !envelope.source.empty() && !envelope.target.empty() && !envelope.topic.empty();
}

std::uint64_t HashCombine(const std::uint64_t seed, const std::uint64_t value) {
    return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}

std::uint64_t HashString(const std::string& value) {
    return std::hash<std::string>{}(value);
}


struct ProtocolCapabilitySnapshot {
    std::uint64_t legacy{1U};
    std::uint64_t compact{1U};
    std::uint64_t protobuf{0U};
    std::uint64_t cbor{0U};
    std::uint64_t last_check_ms{0U};
};

ProtocolCapabilitySnapshot GetProtocolCapabilitySnapshot(const std::uint64_t now_ms) {
    static std::mutex cap_mutex;
    static ProtocolCapabilitySnapshot cached;

    std::lock_guard<std::mutex> lock(cap_mutex);
    if (cached.last_check_ms != 0U && now_ms - cached.last_check_ms < 1000U) {
        return cached;
    }

    cached.legacy = SelfCheckProtocolCapability(MessageProtocolMode::kLegacyPipe) ? 1U : 0U;
    cached.compact = SelfCheckProtocolCapability(MessageProtocolMode::kCompact) ? 1U : 0U;
    cached.protobuf = SelfCheckProtocolCapability(MessageProtocolMode::kProtobufReserved) ? 1U : 0U;
    cached.cbor = SelfCheckProtocolCapability(MessageProtocolMode::kCborReserved) ? 1U : 0U;
    cached.last_check_ms = now_ms;
    return cached;
}

std::uint64_t ComputePolicySignature(const BridgePolicyTable& table) {
    std::uint64_t signature = HashCombine(0U, static_cast<std::uint64_t>(table.default_policy.priority));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.max_end_to_end_latency_ms));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.transport_receive_timeout_ms));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.transport_send_timeout_ms));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.backpressure_policy));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.drop_policy));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.retry_budget.max_retries.value_or(0)));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.retry_budget.initial_backoff_ms));
    signature = HashCombine(signature, static_cast<std::uint64_t>(table.default_policy.retry_budget.max_backoff_ms));

    auto hash_rule = [&signature](const PolicyRule& rule) {
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.priority));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.match_any_channel));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.channel));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.match_any_source));
        signature = HashCombine(signature, HashString(rule.source));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.match_any_target));
        signature = HashCombine(signature, HashString(rule.target));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.match_any_qos));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.qos));
        signature = HashCombine(signature, HashString(rule.topic));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.priority));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.max_end_to_end_latency_ms));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.transport_receive_timeout_ms));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.transport_send_timeout_ms));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.backpressure_policy));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.drop_policy));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.retry_budget.max_retries.value_or(0)));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.retry_budget.initial_backoff_ms));
        signature = HashCombine(signature, static_cast<std::uint64_t>(rule.policy.retry_budget.max_backoff_ms));
    };

    for (const auto& rule : table.template_rules) {
        hash_rule(rule);
    }
    for (const auto& rule : table.rules) {
        hash_rule(rule);
    }
    for (const auto& rule : table.runtime_overrides) {
        hash_rule(rule);
    }
    return signature;
}

}  // namespace

InterconnectBridge::InterconnectBridge(std::unique_ptr<ITransport> vehicle_to_robot_transport,
                                       std::unique_ptr<ITransport> robot_to_vehicle_transport)
    : vehicle_to_robot_transport_(std::move(vehicle_to_robot_transport)),
      robot_to_vehicle_transport_(std::move(robot_to_vehicle_transport)) {}

InterconnectBridge::~InterconnectBridge() {
    Stop();
}

// 从配置提供者启动桥接
vr::core::ErrorCode InterconnectBridge::Start(IConfigProvider* const provider) noexcept {
    if (provider == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    BridgeConfig cfg;
    std::string source;
    const vr::core::ErrorCode ec = provider->LoadBridgeConfig(&cfg, &source);
    if (ec != vr::core::ErrorCode::kOk) {
        return ec;
    }

    loaded_config_source_ = source;
    loaded_config_version_ = provider->GetVersion();
    // 已知良好快照与策略签名**不在这里**取：这里的 cfg 还是 provider 交来的
    // 原始表（未归一化、未填模板规则），拿它算出来的签名描述的不是"实际生效的表"。
    // 统一由 Start(const BridgeConfig&) 在配置完全成型之后再落快照，见那里的注释。
    return Start(cfg);
}

// 启动桥接
vr::core::ErrorCode InterconnectBridge::Start(const BridgeConfig& config) noexcept {
    if (running_.load(std::memory_order_acquire)) {
        return vr::core::ErrorCode::kOk;
    }

    if (loaded_config_source_.empty()) {
        loaded_config_source_ = "direct";
    }

    if (!vehicle_to_robot_transport_ || !robot_to_vehicle_transport_) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    config_ = config;
    policy_manager_.NormalizePolicyDefaults(&config_);
    trace_sample_rate_percent_ = std::min(config_.trace_sample_rate_percent, 100U);
    trace_sample_interval_ms_ = config_.trace_sample_interval_ms == 0U
                                    ? kTraceIdSampleIntervalMsDefault
                                    : config_.trace_sample_interval_ms;
    sla_violation_sample_interval_ms_ = config_.sla_violation_sample_interval_ms == 0U
                                            ? kSlaViolationSampleIntervalMsDefault
                                            : config_.sla_violation_sample_interval_ms;
    diagnostics_report_interval_ms_ = config_.diagnostics_report_interval_ms == 0U
                                          ? 1000U
                                          : config_.diagnostics_report_interval_ms;
    enable_diagnostics_reporting_ = config_.enable_diagnostics_reporting;
    protocol_mode_ = config_.protocol_mode;
    PopulateDefaultTemplateRules(&config_.policy_table);
    RebuildPolicyIndex();

    // 已知良好快照与策略签名必须描述**实际生效**的策略表，即"已归一化 +
    // 已填充默认模板规则"之后的状态。放在这里是本次修复的核心（Bug 1）：
    //   旧实现在 Start(provider) 里趁归一化之前就算了一次签名，而这里的重算
    //   又被 last_known_good_version_ != 0 挡住（provider 的初始 version 是 1），
    //   于是 last_known_good_signature_ 描述"归一化前"的表，
    //   ReloadConfigIfChanged() 的 next_signature 描述"归一化后"的表 ——
    //   两者必然不等，于是每一次 reload 都被判成签名不符并回滚。
    //
    // 注意顺序：快照必须落在 canary 判定**之后**。canary 未命中时要回退到
    // 上一份已知良好配置，若先覆盖了快照，回退就成了"回退到自己"，灰度失效。
    const bool canary_gate_pending = config_.enable_config_canary &&
                                     config_.config_canary_percent < 100U &&
                                     has_committed_config_;
    if (canary_gate_pending) {
        const std::uint32_t gate = static_cast<std::uint32_t>(NowUnixMs() % 100U);
        if (gate >= config_.config_canary_percent) {
            config_ = last_known_good_config_;
            loaded_config_source_ = last_known_good_source_;
            loaded_config_version_ = last_known_good_version_;
            // 回退后策略表换了，索引必须按回退后的表重建，否则索引描述的是
            // 那份被拒绝的配置（原始实现漏了这一步）。
            RebuildPolicyIndex();
            last_loaded_signature_ = last_known_good_signature_;
            reload_rollback_count_.fetch_add(1U, std::memory_order_relaxed);
            reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
            last_reload_status_code_.store(static_cast<std::int32_t>(vr::core::ErrorCode::kWouldBlock),
                                           std::memory_order_relaxed);
            RefreshAggregatedMetrics();
            return vr::core::ErrorCode::kWouldBlock;
        }
    }

    last_known_good_config_ = config_;
    last_known_good_version_ = loaded_config_version_;
    last_known_good_source_ = loaded_config_source_;
    last_known_good_signature_ = ComputePolicySignature(config_.policy_table);
    last_loaded_signature_ = last_known_good_signature_;

    ApplyTransportTuning(config_);

    vr::core::ErrorCode ec = vehicle_to_robot_transport_->Create(config_.vehicle_to_robot_endpoint);
    if (ec != vr::core::ErrorCode::kOk) {
        return ec;
    }

    ec = robot_to_vehicle_transport_->Create(config_.robot_to_vehicle_endpoint);
    if (ec != vr::core::ErrorCode::kOk) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
        return ec;
    }

    ec = worker_pool_.Start(config_.thread_pool);
    if (ec != vr::core::ErrorCode::kOk) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
        robot_to_vehicle_transport_->Close();
        robot_to_vehicle_transport_->Unlink();
        return ec;
    }

    running_.store(true, std::memory_order_release);

    ec = worker_pool_.Enqueue([this]() { VehicleInboundLoop(); });
    if (ec != vr::core::ErrorCode::kOk && ec != vr::core::ErrorCode::kThreadTaskRejected) {
        Stop();
        return ec;
    }

    ec = worker_pool_.Enqueue([this]() { RobotInboundLoop(); });
    if (ec != vr::core::ErrorCode::kOk && ec != vr::core::ErrorCode::kThreadTaskRejected) {
        Stop();
        return ec;
    }

    // 启动成功 = 这份配置已成为"已知良好配置"，后续 Start/reload 才允许做灰度回退。
    has_committed_config_ = true;
    RefreshAggregatedMetrics();
    return vr::core::ErrorCode::kOk;
}

// 停止桥接
void InterconnectBridge::Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    worker_pool_.Stop();

    if (vehicle_to_robot_transport_) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
    }

    if (robot_to_vehicle_transport_) {
        robot_to_vehicle_transport_->Close();
        robot_to_vehicle_transport_->Unlink();
    }

    RefreshAggregatedMetrics(true);
}

vr::core::ErrorCode InterconnectBridge::PublishFromVehicle(const MessageEnvelope& envelope) noexcept {
    return Publish(vehicle_to_robot_transport_.get(), envelope);
}

vr::core::ErrorCode InterconnectBridge::PublishFromRobot(const MessageEnvelope& envelope) noexcept {
    return Publish(robot_to_vehicle_transport_.get(), envelope);
}

MessageRouter& InterconnectBridge::VehicleRouter() noexcept {
    return vehicle_router_;
}

MessageRouter& InterconnectBridge::RobotRouter() noexcept {
    return robot_router_;
}

BridgeMetrics InterconnectBridge::GetBridgeMetrics() const noexcept {
    return metrics_aggregator_.GetBridgeMetrics();
}

vr::core::ThreadPoolMetrics InterconnectBridge::GetThreadPoolMetrics() const noexcept {
    return metrics_aggregator_.GetThreadPoolMetrics();
}

MetricsSnapshot InterconnectBridge::CaptureMetricsSnapshot() noexcept {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.CaptureSnapshot();
}

MetricsDelta InterconnectBridge::ExportMetricsDelta() noexcept {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.ExportDeltaSinceLastCall();
}

// 导出 Prometheus 文本。
// 进入时先强刷：本族 API 里 ExportMetricsDelta() 一直是这么做的，而
// RefreshAggregatedMetrics 的注释明写「force=true：用于关键路径（如配置提交/回滚、
// 快照导出前）强制刷新，保证观测一致性」。同族里一个遵守、两个不遵守 ⇒ 这是 bug，
// 不是设计选择。不修的话，"事件刚发生就抓 Prometheus 必须看到"会静默失败（滞后 ≤100ms）。
// 代价：每次调用 = 一次完整聚合（含 9 张 map 深拷贝），**不要放进高频循环**。
std::string InterconnectBridge::ExportPrometheusMetrics() const {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.ExportPrometheus();
}

// 导出 JSON。理由同 ExportPrometheusMetrics()：见上。
std::string InterconnectBridge::ExportJsonMetrics() const {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.ExportJson();
}

// 轻量 JSON：**故意保持不强刷**，读缓存（最多滞后 100ms）。
// 「轻」指的是**输出字段更少** —— 它是全量 JSON 的**子集**，省掉
// idempotency_drop / backpressure_drop / reload 等一批计数器（实测：判重发生
// 刚发生时，这里的 tx 仍是旧值、且完全不含 idempotency_drop 字段）。
// **不指"更新鲜"**：若将来有人需要"刚发生必可见"的轻量视图，应该在
// CaptureMetricsSnapshot() 之上裁字段，而不是指望这个接口。
std::string InterconnectBridge::ExportJsonMetricsLightweight() const {
    return metrics_aggregator_.ExportJsonLightweight();
}

std::string InterconnectBridge::GetLoadedConfigSource() const {
    return loaded_config_source_;
}

std::string InterconnectBridge::GetLastReloadAuditSummary() const {
    return last_reload_audit_summary_;
}

void InterconnectBridge::SetDiagnosticsReporter(std::shared_ptr<IDiagnosticsReporter> reporter) {
    diagnostics_reporter_ = std::move(reporter);
}

void InterconnectBridge::SetMessageAuthenticator(
    std::shared_ptr<IMessageAuthenticator> authenticator) {
    authenticator_ = std::move(authenticator);
}

std::uint64_t InterconnectBridge::GetLoadedConfigVersion() const noexcept {
    return loaded_config_version_;
}

std::string InterconnectBridge::GetPolicyLintReport() const {
    std::lock_guard<std::mutex> lock(policy_lint_mutex_);
    if (!policy_lint_cache_valid_) {
        policy_lint_report_ = BuildPolicyLintReport();
        policy_lint_cache_valid_ = true;
    }
    return policy_lint_report_;
}


std::vector<std::string> InterconnectBridge::DumpPolicyConflicts() const {
    return CollectPolicyConflicts();
}

std::string InterconnectBridge::ExportPolicyEffectiveView() const {
    std::ostringstream oss;
    oss << "policy_effective_view:{";
    oss << "default_policy_priority=" << config_.policy_table.default_policy.priority << ",";
    oss << "rules=" << config_.policy_table.rules.size() << ",";
    oss << "templates=" << config_.policy_table.template_rules.size() << ",";
    oss << "overrides=" << config_.policy_table.runtime_overrides.size() << ",";
    oss << "cache_size=" << policy_cache_items_.size() << ",";
    oss << "cache_hits=" << policy_cache_hit_count_.load(std::memory_order_relaxed) << ",";
    oss << "cache_miss=" << policy_cache_miss_count_.load(std::memory_order_relaxed) << ",";
    oss << "policy_conflicts=" << policy_conflict_count_.load(std::memory_order_relaxed);
    oss << "}";
    return oss.str();
}

std::string InterconnectBridge::DumpRuntimeState() const {
    diag_dump_state_count_.fetch_add(1U, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << "{";
    oss << "\"loaded_config_version\":" << loaded_config_version_ << ",";
    oss << "\"loaded_config_source\":\"" << loaded_config_source_ << "\",";
    oss << "\"policy_conflicts\":" << CollectPolicyConflicts().size() << ",";
    oss << "\"policy_cache_size\":" << policy_cache_items_.size() << ",";
    oss << "\"inflight_vehicle\":" << inflight_vehicle_.load(std::memory_order_relaxed) << ",";
    oss << "\"inflight_robot\":" << inflight_robot_.load(std::memory_order_relaxed) << ",";
    oss << "\"failover_v2r\":" << (failover_from_vehicle_to_robot_enabled_ ? 1 : 0) << ",";
    oss << "\"failover_r2v\":" << (failover_from_robot_to_vehicle_enabled_ ? 1 : 0) << ",";
    oss << "\"failover_hit\":" << failover_hit_count_.load(std::memory_order_relaxed) << ",";
    oss << "\"health_primary\":" << transport_primary_healthy_.load(std::memory_order_relaxed)
        << ",";
    oss << "\"health_secondary\":" << transport_secondary_healthy_.load(std::memory_order_relaxed);
    oss << "}";
    return oss.str();
}

std::string InterconnectBridge::ExecuteDiagnosticCommand(const std::string& command) const {
    if (command == "dump runtime") {
        return DumpRuntimeState();
    }
    if (command == "dump policy") {
        return GetPolicyLintReport();
    }
    if (command == "policy effective") {
        return ExportPolicyEffectiveView();
    }
    if (command == "dump transport") {
        std::ostringstream oss;
        oss << "transport:{primary_healthy="
            << transport_primary_healthy_.load(std::memory_order_relaxed)
            << ",secondary_healthy="
            << transport_secondary_healthy_.load(std::memory_order_relaxed)
            << ",failover_hits="
            << failover_hit_count_.load(std::memory_order_relaxed) << "}";
        return oss.str();
    }
    if (command == "dump cache") {
        std::ostringstream oss;
        oss << "cache:{policy_cache_size=" << policy_cache_items_.size() << "}";
        return oss.str();
    }

    return "diag:{error=unknown_command}";
}

bool InterconnectBridge::RuleMatches(const PolicyRule& left,
                                    const PolicyRule& right) const noexcept {
    if (!left.match_any_channel && !right.match_any_channel && left.channel != right.channel) {
        return false;
    }
    if (!left.match_any_source && !right.match_any_source && left.source != right.source) {
        return false;
    }
    if (!left.match_any_target && !right.match_any_target && left.target != right.target) {
        return false;
    }
    if (!left.match_any_qos && !right.match_any_qos && left.qos != right.qos) {
        return false;
    }
    if (!left.topic.empty() && left.topic != "*" &&
        !right.topic.empty() && right.topic != "*" &&
        left.topic != right.topic) {
        return false;
    }
    return true;
}

std::string InterconnectBridge::FormatRuleKey(const PolicyRule& rule) const {
    const std::string channel = rule.match_any_channel ? "*" : std::to_string(static_cast<int>(rule.channel));
    const std::string qos = rule.match_any_qos ? "*" : std::to_string(static_cast<int>(rule.qos));
    const std::string source = rule.match_any_source ? "*" : rule.source;
    const std::string target = rule.match_any_target ? "*" : rule.target;
    const std::string topic = rule.topic.empty() ? "*" : rule.topic;
    return "topic=" + topic + ",channel=" + channel + ",qos=" + qos +
           ",source=" + source + ",target=" + target;
}

std::string InterconnectBridge::FormatRuleSummary(const PolicyRule& rule) const {
    const auto policy = std::string("sla=") + std::to_string(rule.policy.max_end_to_end_latency_ms) +
        "ms,tx_to=" + std::to_string(rule.policy.transport_send_timeout_ms) +
        "ms,rx_to=" + std::to_string(rule.policy.transport_receive_timeout_ms) + "ms" +
        ",sla_priority=" + std::to_string(rule.policy.priority);
    const auto backpressure = std::string("backpressure=") +
        std::to_string(static_cast<int>(rule.policy.backpressure_policy));
    const auto drop_policy = std::string("drop=") +
        std::to_string(static_cast<int>(rule.policy.drop_policy));
    const auto retry_policy = std::string("retry=") +
        std::to_string(rule.policy.retry_budget.max_retries.value_or(0)) + "/" +
        std::to_string(rule.policy.retry_budget.initial_backoff_ms) + "/" +
        std::to_string(rule.policy.retry_budget.max_backoff_ms);
    return "priority=" + std::to_string(rule.priority) + " {" + FormatRuleKey(rule) +
           "," + policy + "," + backpressure + "," + drop_policy + "," + retry_policy + "}";
}

std::vector<std::string> InterconnectBridge::CollectPolicyConflicts() const {
    std::vector<std::string> conflicts;

    auto scan_rules = [&](const std::string& label, const std::vector<PolicyRule>& rules) {
        if (rules.empty()) {
            return;
        }
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const PolicyRule& lhs = rules[i];
            for (std::size_t j = i + 1; j < rules.size(); ++j) {
                const PolicyRule& rhs = rules[j];
                if (lhs.priority == rhs.priority && RuleMatches(lhs, rhs)) {
                    conflicts.push_back(label + " conflict: " + FormatRuleSummary(lhs) +
                                        " vs " + FormatRuleSummary(rhs));
                }
            }
        }
    };

    scan_rules("template", config_.policy_table.template_rules);
    scan_rules("rules", config_.policy_table.rules);
    scan_rules("overrides", config_.policy_table.runtime_overrides);

    return conflicts;
}

std::string InterconnectBridge::BuildPolicyLintReport() const {
    std::ostringstream oss;
    std::vector<std::string> issues;
    std::vector<std::string> warnings;

    auto validate_default = [&](const BridgeSlaPolicy& policy, const std::string& label) {
        if (policy.transport_receive_timeout_ms < 0 || policy.transport_send_timeout_ms < 0) {
            issues.push_back(label + ": negative timeout");
        }
        if (policy.retry_budget.max_retries.value_or(0) < 0 ||
            policy.retry_budget.initial_backoff_ms < 0 ||
            policy.retry_budget.max_backoff_ms < policy.retry_budget.initial_backoff_ms) {
            issues.push_back(label + ": retry budget invalid");
        }
        if (policy.drop_policy == DropPolicy::kDropOldest &&
            policy.backpressure_policy != BackpressurePolicy::kDropOldest) {
            warnings.push_back(label + ": drop_oldest without backpressure drop_oldest");
        }
    };

    auto collect_issues = [&](const std::string& label, const std::vector<PolicyRule>& rules) {
        if (rules.empty()) {
            warnings.push_back(label + ": empty rule set");
            return;
        }
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const PolicyRule& rule = rules[i];
            if (rule.topic.empty()) {
                issues.push_back(label + " rule[" + std::to_string(i) + "] missing topic");
            }
            if (rule.policy.transport_receive_timeout_ms < 0 ||
                rule.policy.transport_send_timeout_ms < 0) {
                issues.push_back(label + " rule[" + std::to_string(i) + "] has negative timeout");
            }
            if (rule.policy.retry_budget.max_retries.value_or(0) < 0 ||
                rule.policy.retry_budget.initial_backoff_ms < 0 ||
                rule.policy.retry_budget.max_backoff_ms <
                    rule.policy.retry_budget.initial_backoff_ms) {
                issues.push_back(label + " rule[" + std::to_string(i) + "] retry budget invalid");
            }
            if (rule.policy.drop_policy == DropPolicy::kDropOldest &&
                rule.policy.backpressure_policy != BackpressurePolicy::kDropOldest) {
                warnings.push_back(label + " rule[" + std::to_string(i) + "] drop_oldest without backpressure drop_oldest");
            }
        }
    };

    validate_default(config_.policy_table.default_policy, "default_policy");
    collect_issues("template_rules", config_.policy_table.template_rules);
    collect_issues("rules", config_.policy_table.rules);
    collect_issues("runtime_overrides", config_.policy_table.runtime_overrides);

    const auto conflicts = CollectPolicyConflicts();
    if (!conflicts.empty()) {
        issues.insert(issues.end(), conflicts.begin(), conflicts.end());
    }

    oss << "policy_lint:\n";
    if (issues.empty() && warnings.empty()) {
        oss << "  status: ok\n";
        return oss.str();
    }

    if (!issues.empty()) {
        oss << "  status: issues(" << issues.size() << ")\n";
        for (const auto& issue : issues) {
            oss << "  - " << issue << "\n";
        }
    }

    if (!warnings.empty()) {
        oss << "  warnings(" << warnings.size() << ")\n";
        for (const auto& warning : warnings) {
            oss << "  - " << warning << "\n";
        }
    }

    const auto summary = BuildPolicyLintSummary();
    oss << "  summary: " << summary << "\n";

    return oss.str();
}

std::pair<std::uint64_t, std::uint64_t> InterconnectBridge::GetPolicyLintCounts() const {
    std::uint64_t issue_count = 0U;
    std::uint64_t warning_count = 0U;

    auto count = [&](const BridgeSlaPolicy& policy) {
        if (policy.transport_receive_timeout_ms < 0 || policy.transport_send_timeout_ms < 0) {
            issue_count += 1U;
        }
        if (policy.retry_budget.max_retries.value_or(0) < 0 ||
            policy.retry_budget.initial_backoff_ms < 0 ||
            policy.retry_budget.max_backoff_ms < policy.retry_budget.initial_backoff_ms) {
            issue_count += 1U;
        }
        if (policy.drop_policy == DropPolicy::kDropOldest &&
            policy.backpressure_policy != BackpressurePolicy::kDropOldest) {
            warning_count += 1U;
        }
    };

    auto scan_rules = [&](const std::vector<PolicyRule>& rules) {
        for (const auto& rule : rules) {
            if (rule.topic.empty()) {
                issue_count += 1U;
            }
            if (rule.policy.transport_receive_timeout_ms < 0 ||
                rule.policy.transport_send_timeout_ms < 0) {
                issue_count += 1U;
            }
            if (rule.policy.retry_budget.max_retries.value_or(0) < 0 ||
                rule.policy.retry_budget.initial_backoff_ms < 0 ||
                rule.policy.retry_budget.max_backoff_ms <
                    rule.policy.retry_budget.initial_backoff_ms) {
                issue_count += 1U;
            }
            if (rule.policy.drop_policy == DropPolicy::kDropOldest &&
                rule.policy.backpressure_policy != BackpressurePolicy::kDropOldest) {
                warning_count += 1U;
            }
        }
    };

    count(config_.policy_table.default_policy);
    scan_rules(config_.policy_table.template_rules);
    scan_rules(config_.policy_table.rules);
    scan_rules(config_.policy_table.runtime_overrides);

    issue_count += static_cast<std::uint64_t>(CollectPolicyConflicts().size());

    return {issue_count, warning_count};
}

std::string InterconnectBridge::BuildPolicyLintSummary() const {
    const auto counts = GetPolicyLintCounts();
    std::ostringstream oss;
    oss << "issues=" << counts.first << ",warnings=" << counts.second;
    return oss.str();
}

// 重新构建策略索引
void InterconnectBridge::RebuildPolicyIndex() {
    config_.policy_table.template_topic_rule_index.clear();
    config_.policy_table.topic_rule_index.clear();
    config_.policy_table.override_topic_rule_index.clear();

    auto index_rules = [](const std::vector<PolicyRule>& rules,
                          std::unordered_map<std::string, std::vector<std::size_t>>* index) {
        index->clear();
        index->reserve(rules.size());
        for (std::size_t i = 0; i < rules.size(); ++i) {
            const auto& rule = rules[i];
            if (!rule.topic.empty() && rule.topic != "*") {
                (*index)[rule.topic].push_back(i);
            }
        }
    };

    index_rules(config_.policy_table.template_rules,
                &config_.policy_table.template_topic_rule_index);
    index_rules(config_.policy_table.rules, &config_.policy_table.topic_rule_index);
    index_rules(config_.policy_table.runtime_overrides,
                &config_.policy_table.override_topic_rule_index);

    {
        std::lock_guard<std::mutex> lock(policy_cache_mutex_);
        policy_cache_index_.clear();
        policy_cache_items_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(policy_lint_mutex_);
        policy_lint_cache_valid_ = false;
        policy_lint_report_.clear();
    }
}

void InterconnectBridge::PopulateDefaultTemplateRules(BridgePolicyTable* const table) {
    if (table == nullptr || !table->template_rules.empty()) {
        return;
    }

    PolicyRule control_rule;
    control_rule.priority = 10U;
    control_rule.match_any_channel = false;
    control_rule.channel = ChannelType::kControl;
    control_rule.topic = "*";
    control_rule.policy.max_end_to_end_latency_ms = 50U;
    control_rule.policy.backpressure_policy = BackpressurePolicy::kDropOldest;
    control_rule.policy.drop_policy = DropPolicy::kDropOldest;

    PolicyRule telemetry_rule;
    telemetry_rule.priority = 5U;
    telemetry_rule.match_any_channel = false;
    telemetry_rule.channel = ChannelType::kTelemetry;
    telemetry_rule.topic = "*";
    telemetry_rule.policy.max_end_to_end_latency_ms = 150U;
    telemetry_rule.policy.backpressure_policy = BackpressurePolicy::kReject;

    table->template_rules.push_back(control_rule);
    table->template_rules.push_back(telemetry_rule);
}

// 是否允许记录日志
std::uint64_t InterconnectBridge::NextLogAllowedMs(std::atomic<std::uint64_t>* timestamp,
                                                  const std::uint64_t now_ms,
                                                  const std::uint64_t interval_ms) noexcept {
    const std::uint64_t next_allowed = timestamp->load(std::memory_order_relaxed);
    if (now_ms < next_allowed) {
        return next_allowed;
    }

    std::uint64_t expected = next_allowed;
    const std::uint64_t desired = now_ms + interval_ms;
    timestamp->compare_exchange_strong(expected, desired, std::memory_order_relaxed);
    return now_ms;
}

bool InterconnectBridge::RuleMatches(const PolicyRule& rule,
                                    const MessageEnvelope& envelope) const noexcept {
    if (!rule.match_any_channel && rule.channel != envelope.channel) {
        return false;
    }
    if (!rule.match_any_source && rule.source != envelope.source) {
        return false;
    }
    if (!rule.match_any_target && rule.target != envelope.target) {
        return false;
    }
    if (!rule.match_any_qos && rule.qos != envelope.qos) {
        return false;
    }
    if (!rule.topic.empty() && rule.topic != "*" && rule.topic != envelope.topic) {
        return false;
    }
    return true;
}

const PolicyCacheEntry* InterconnectBridge::LookupPolicyCache(
    const MessageEnvelope& envelope) const noexcept {
    const PolicyCacheKey key{envelope.topic, envelope.channel, envelope.qos};
    std::lock_guard<std::mutex> lock(policy_cache_mutex_);
    auto it = policy_cache_index_.find(key);
    if (it == policy_cache_index_.end()) {
        return nullptr;
    }

    policy_cache_items_.splice(policy_cache_items_.begin(), policy_cache_items_, it->second);
    it->second = policy_cache_items_.begin();
    return &it->second->entry;
}

const PolicyCacheEntry* InterconnectBridge::StorePolicyCache(
    const MessageEnvelope& envelope, const BridgeSlaPolicy& policy,
    const bool override_applied, const bool conflict_detected) const noexcept {
    const PolicyCacheKey key{envelope.topic, envelope.channel, envelope.qos};

    std::lock_guard<std::mutex> lock(policy_cache_mutex_);
    auto it = policy_cache_index_.find(key);
    if (it != policy_cache_index_.end()) {
        it->second->entry = {policy, override_applied, conflict_detected};
        policy_cache_items_.splice(policy_cache_items_.begin(), policy_cache_items_, it->second);
        it->second = policy_cache_items_.begin();
        return &it->second->entry;
    }

    policy_cache_items_.push_front({key, {policy, override_applied, conflict_detected}});
    policy_cache_index_[key] = policy_cache_items_.begin();

    if (policy_cache_items_.size() > kPolicyCacheMaxEntries) {
        const auto last = std::prev(policy_cache_items_.end());
        policy_cache_index_.erase(last->key);
        policy_cache_items_.pop_back();
    }

    return &policy_cache_items_.front().entry;
}

const BridgeSlaPolicy* InterconnectBridge::FindBestPolicyMatch(
    const std::vector<PolicyRule>& rules,
    const std::unordered_map<std::string, std::vector<std::size_t>>& index,
    const MessageEnvelope& envelope, std::uint32_t* out_priority,
    bool* out_conflict_detected) const noexcept {
    if (out_priority == nullptr || out_conflict_detected == nullptr) {
        return nullptr;
    }

    const std::vector<std::size_t>* candidate_indices = nullptr;
    const auto it = index.find(envelope.topic);
    if (it != index.end()) {
        candidate_indices = &it->second;
    }

    const std::vector<PolicyRule>* source_rules = &rules;
    std::vector<std::size_t> fallback_indices;
    if (candidate_indices == nullptr) {
        fallback_indices.reserve(rules.size());
        for (std::size_t i = 0; i < rules.size(); ++i) {
            fallback_indices.push_back(i);
        }
        candidate_indices = &fallback_indices;
    }

    const BridgeSlaPolicy* best_policy = nullptr;
    std::uint32_t best_priority = 0U;
    bool conflict = false;

    for (const std::size_t index_entry : *candidate_indices) {
        if (index_entry >= source_rules->size()) {
            continue;
        }
        const PolicyRule& rule = (*source_rules)[index_entry];
        if (!RuleMatches(rule, envelope)) {
            continue;
        }

        if (best_policy == nullptr || rule.priority > best_priority) {
            best_policy = &rule.policy;
            best_priority = rule.priority;
            conflict = false;
        } else if (rule.priority == best_priority) {
            conflict = true;
        }
    }

    *out_priority = best_priority;
    *out_conflict_detected = conflict;
    return best_policy;
}

const BridgeSlaPolicy& InterconnectBridge::ResolvePolicyInternal(
    const MessageEnvelope& envelope, bool* out_override_applied,
    bool* out_conflict_detected) const noexcept {
    if (out_override_applied == nullptr || out_conflict_detected == nullptr) {
        return config_.policy_table.default_policy;
    }

    *out_override_applied = false;
    *out_conflict_detected = false;

    std::uint32_t override_priority = 0U;
    bool override_conflict = false;
    const BridgeSlaPolicy* override_policy = FindBestPolicyMatch(
        config_.policy_table.runtime_overrides,
        config_.policy_table.override_topic_rule_index,
        envelope, &override_priority, &override_conflict);

    if (override_conflict) {
        *out_conflict_detected = true;
    }

    std::uint32_t rule_priority = 0U;
    bool rule_conflict = false;
    const BridgeSlaPolicy* rule_policy = FindBestPolicyMatch(
        config_.policy_table.rules,
        config_.policy_table.topic_rule_index,
        envelope, &rule_priority, &rule_conflict);

    if (rule_conflict) {
        *out_conflict_detected = true;
    }

    std::uint32_t template_priority = 0U;
    bool template_conflict = false;
    const BridgeSlaPolicy* template_policy = FindBestPolicyMatch(
        config_.policy_table.template_rules,
        config_.policy_table.template_topic_rule_index,
        envelope, &template_priority, &template_conflict);

    if (template_conflict) {
        *out_conflict_detected = true;
    }

    // 命中优先级：override > rule > template。三个候选的优先级数值并不在这里
    // 落地成任何输出，只用于紧随其后的冲突判定（见 :885-:894）。
    // 发送用的调度优先级由 ResolvePriorityForPublish() 独立决定。
    const BridgeSlaPolicy* selected_policy = nullptr;
    if (override_policy != nullptr) {
        selected_policy = override_policy;
        *out_override_applied = true;
    } else if (rule_policy != nullptr) {
        selected_policy = rule_policy;
    } else if (template_policy != nullptr) {
        selected_policy = template_policy;
    }

    if (override_policy != nullptr && rule_policy != nullptr && override_priority < rule_priority) {
        *out_conflict_detected = true;
    }
    if (override_policy != nullptr && template_policy != nullptr &&
        override_priority < template_priority) {
        *out_conflict_detected = true;
    }
    if (rule_policy != nullptr && template_policy != nullptr && rule_priority < template_priority) {
        *out_conflict_detected = true;
    }

    if (selected_policy != nullptr) {
        return *selected_policy;
    }

    return config_.policy_table.default_policy;
}

// 重新加载配置（关键稳定路径）
// 流程：provider reload -> load config -> 归一化 -> 签名与合法性校验 -> 成功提交或回滚。
// 设计约束：任何中间失败都必须保持“上一个已知良好配置”可用，并输出可审计原因。
//
// ⚠️ 已知空白（P1，勿误判）：本函数**没有 config-canary 灰度门禁**。
//    Start() 里那段 `canary_gate_pending` 只作用于启动路径，不要因为看过那里就以为
//    reload 也被灰度保护 —— 目前通过热更新下发的新配置会直接全量生效（策略字段除外，
//    会被判为 policy_fields_unsupported 并回滚）。
//    影响与待决策的语义选项见：README「已知限制」段、docs/backlog.md BL-01。
vr::core::ErrorCode InterconnectBridge::ReloadConfigIfChanged(
    IConfigProvider* const provider) noexcept {
    if (provider == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    constexpr const char* kReloadReasonProviderReloadFail = "provider_reload_fail";
    constexpr const char* kReloadReasonProviderLoadFail = "provider_load_fail";
    constexpr const char* kReloadReasonConfigInvalid = "config_invalid";
    constexpr const char* kReloadReasonSignatureMismatch = "signature_mismatch";
    // 与 signature_mismatch 分开：这是**产品限制**（reload 不支持改策略字段），
    // 不是配置损坏。两者排查路径不同，混在一起会把 5 分钟的问题拖成半天。
    constexpr const char* kReloadReasonPolicyFieldsUnsupported = "policy_fields_unsupported";

    auto should_preserve_locked_policy = [&](const BridgeConfig& next_cfg) {
        if (!config_.policy_table.default_policy.lock_policy) {
            return true;
        }

        const BridgeSlaPolicy& current = config_.policy_table.default_policy;
        const BridgeSlaPolicy& candidate = next_cfg.policy_table.default_policy;
        const bool same_priority = current.priority == candidate.priority;
        const bool same_latency = current.max_end_to_end_latency_ms == candidate.max_end_to_end_latency_ms;
        const bool same_recv = current.transport_receive_timeout_ms == candidate.transport_receive_timeout_ms;
        const bool same_send = current.transport_send_timeout_ms == candidate.transport_send_timeout_ms;
        const bool same_backpressure = current.backpressure_policy == candidate.backpressure_policy;
        const bool same_drop = current.drop_policy == candidate.drop_policy;
        const bool same_retry = current.retry_budget.max_retries.value_or(0) == candidate.retry_budget.max_retries.value_or(0) &&
            current.retry_budget.initial_backoff_ms == candidate.retry_budget.initial_backoff_ms &&
            current.retry_budget.max_backoff_ms == candidate.retry_budget.max_backoff_ms;
        return same_latency && same_recv && same_send && same_backpressure && same_drop && same_retry &&
               same_priority;
    };

    const vr::core::ErrorCode reload_ec = provider->Reload();
    if (reload_ec != vr::core::ErrorCode::kOk) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        last_reload_status_code_.store(static_cast<std::int32_t>(reload_ec),
                                       std::memory_order_relaxed);
        reload_rollback_reason_code_.store(1, std::memory_order_relaxed);
        last_reload_audit_summary_ = std::string("reload_audit:{reload_stage=provider_reload,status=fail,reason=") +
            kReloadReasonProviderReloadFail + "}";
        return reload_ec;
    }

    const std::uint64_t new_version = provider->GetVersion();
    if (new_version == loaded_config_version_) {
        last_reload_status_code_.store(static_cast<std::int32_t>(vr::core::ErrorCode::kOk),
                                       std::memory_order_relaxed);
        reload_rollback_reason_code_.store(0, std::memory_order_relaxed);
        last_reload_audit_summary_ = "reload_audit:{status=noop}";
        return vr::core::ErrorCode::kOk;
    }

    BridgeConfig cfg;
    std::string source;
    const vr::core::ErrorCode load_ec = provider->LoadBridgeConfig(&cfg, &source);
    if (load_ec != vr::core::ErrorCode::kOk) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        last_reload_status_code_.store(static_cast<std::int32_t>(load_ec),
                                       std::memory_order_relaxed);
        reload_rollback_reason_code_.store(2, std::memory_order_relaxed);
        last_reload_audit_summary_ = std::string("reload_audit:{reload_stage=provider_load,status=fail,reason=") +
            kReloadReasonProviderLoadFail + "}";
        return load_ec;
    }

    BridgeConfig next_config = cfg;
    policy_manager_.NormalizePolicyDefaults(&next_config);
    // 候选配置先补上默认模板规则，再做签名。签名必须与 Start() 里那份
    // "已归一化 + 已填模板"的表比较，否则比的是两个不同的状态（Bug 1 的另一半）。
    PopulateDefaultTemplateRules(&next_config.policy_table);

    const std::uint64_t next_signature = ComputePolicySignature(next_config.policy_table);

    std::uint32_t risk_score = 0U;
    if (next_config.policy_table.default_policy.max_end_to_end_latency_ms !=
        config_.policy_table.default_policy.max_end_to_end_latency_ms) {
        ++risk_score;
    }
    if (next_config.protocol_mode != config_.protocol_mode) {
        ++risk_score;
    }
    if (next_config.thread_pool.worker_count != config_.thread_pool.worker_count) {
        ++risk_score;
    }

    if (config_.enable_config_risk_guard && config_.auto_rollback_on_high_risk &&
        risk_score >= config_.high_risk_block_threshold) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        reload_rollback_count_.fetch_add(1U, std::memory_order_relaxed);
        last_reload_status_code_.store(static_cast<std::int32_t>(vr::core::ErrorCode::kInvalidParam),
                                       std::memory_order_relaxed);
        reload_rollback_reason_code_.store(6, std::memory_order_relaxed);
        last_reload_audit_summary_ = "reload_audit:{status=rollback,reason=high_risk_config_change}";
        diagnostics_manager_.RecordSnapshot(config_.diagnostics_snapshot_path,
                                           config_.diagnostics_snapshot_limit,
                                           "high_risk_config_change",
                                           nullptr);
        diagnostic_snapshot_write_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics(true);
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (!should_preserve_locked_policy(next_config)) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        reload_rollback_count_.fetch_add(1U, std::memory_order_relaxed);
        last_reload_status_code_.store(static_cast<std::int32_t>(vr::core::ErrorCode::kInvalidParam),
                                       std::memory_order_relaxed);
        reload_rollback_reason_code_.store(5, std::memory_order_relaxed);
        last_reload_audit_summary_ = "reload_audit:{status=rollback,reason=policy_lock_violation}";
        RefreshAggregatedMetrics(true);
        return vr::core::ErrorCode::kInvalidParam;
    }

    const BridgeConfig previous_config = config_;
    const std::string previous_source = loaded_config_source_;
    const std::uint64_t previous_version = loaded_config_version_;
    const std::uint64_t previous_signature = last_loaded_signature_;

    std::ostringstream audit;
    audit << "reload_audit:{src:" << previous_source << "->" << source
          << ",ver:" << previous_version << "->" << new_version
          << ",sig:" << previous_signature << "->" << next_signature << "}";

    config_ = next_config;
    trace_sample_rate_percent_ = std::min(config_.trace_sample_rate_percent, 100U);
    trace_sample_interval_ms_ = config_.trace_sample_interval_ms == 0U
                                    ? kTraceIdSampleIntervalMsDefault
                                    : config_.trace_sample_interval_ms;
    sla_violation_sample_interval_ms_ = config_.sla_violation_sample_interval_ms == 0U
                                            ? kSlaViolationSampleIntervalMsDefault
                                            : config_.sla_violation_sample_interval_ms;
    diagnostics_report_interval_ms_ = config_.diagnostics_report_interval_ms == 0U
                                          ? 1000U
                                          : config_.diagnostics_report_interval_ms;
    enable_diagnostics_reporting_ = config_.enable_diagnostics_reporting;
    // next_config 上面已经填过模板规则；这里再调一次是幂等兜底，
    // 保证"落盘后的 config_ 一定带着模板规则"这一不变量在任何路径下都成立。
    PopulateDefaultTemplateRules(&config_.policy_table);
    RebuildPolicyIndex();

    const bool config_valid = config_.thread_pool.worker_count > 0U &&
        config_.thread_pool.queue_capacity > 0U &&
        !config_.vehicle_to_robot_endpoint.name.empty() &&
        !config_.robot_to_vehicle_endpoint.name.empty() &&
        config_.policy_table.default_policy.retry_budget.initial_backoff_ms >= 0 &&
        config_.policy_table.default_policy.retry_budget.max_backoff_ms >=
            config_.policy_table.default_policy.retry_budget.initial_backoff_ms;
    const bool signature_ok = (last_known_good_signature_ == 0U) ||
        (next_signature == last_known_good_signature_);

    // 回滚原因要**分类**，不能一律叫"签名不符"。
    // 运维看到"签名不符"会去查"我是不是改错文件了 / 配置是不是损坏了"，
    // 而真实原因往往是"改了 reload 本就不支持的策略字段"——两者排查路径完全不同。
    // 判据：把候选签名与"当前生效配置"的签名比。不等 = 候选确实改了策略字段；
    // 相等但仍与 last_known_good 不符 = 记账不一致（防御性分支，理论上不该出现）。
    const std::uint64_t current_signature = ComputePolicySignature(previous_config.policy_table);
    const bool policy_fields_changed = (next_signature != current_signature);

    enum class RollbackCause { kConfigInvalid, kPolicyFieldsUnsupported, kSignatureMismatch };
    RollbackCause cause = RollbackCause::kSignatureMismatch;
    if (signature_ok) {
        cause = RollbackCause::kConfigInvalid;
    } else if (policy_fields_changed) {
        cause = RollbackCause::kPolicyFieldsUnsupported;
    }

    if (!config_valid || !signature_ok) {
        config_ = previous_config;
        loaded_config_source_ = previous_source;
        loaded_config_version_ = previous_version;
        last_loaded_signature_ = previous_signature;
        reload_rollback_count_.fetch_add(1U, std::memory_order_relaxed);
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        const vr::core::ErrorCode status = (cause == RollbackCause::kConfigInvalid)
            ? vr::core::ErrorCode::kInvalidParam
            : vr::core::ErrorCode::kQueueCreateFailed;
        last_reload_status_code_.store(static_cast<std::int32_t>(status),
                                       std::memory_order_relaxed);

        // 原因码是外部契约（metrics.reload_rollback_reason_code）：
        //   3 = config_invalid          （配置本身非法，例如 worker_count = 0）
        //   4 = signature_mismatch      （真的对不上，防御性保留）
        //   7 = policy_fields_unsupported（改了 reload 不支持的策略字段）
        // 5/6 已被"策略锁违反 / 高风险自动回滚"占用，不要复用。
        std::int32_t reason_code = 4;
        const char* rollback_reason = kReloadReasonSignatureMismatch;
        if (cause == RollbackCause::kConfigInvalid) {
            reason_code = 3;
            rollback_reason = kReloadReasonConfigInvalid;
        } else if (cause == RollbackCause::kPolicyFieldsUnsupported) {
            reason_code = 7;
            rollback_reason = kReloadReasonPolicyFieldsUnsupported;
        }
        reload_rollback_reason_code_.store(reason_code, std::memory_order_relaxed);

        last_reload_audit_summary_ = audit.str() + ",status=rollback,reason=" + rollback_reason;
        if (cause == RollbackCause::kPolicyFieldsUnsupported) {
            // 附加可操作的说明：让看懂日志的人知道该往哪查，而不是去查文件是否损坏。
            last_reload_audit_summary_ +=
                ",note=reload_supports_non_policy_fields_only_restart_required";
            LOG_WARN(std::string("Bridge config reload rejected: policy fields changed (")
                     + rollback_reason +
                     "). Hot reload only supports non-policy fields; restart to apply "
                     "policy_table / sla_policy changes.");
        }
        RefreshAggregatedMetrics(true);
        return status;
    }

    loaded_config_source_ = source;
    loaded_config_version_ = new_version;
    last_loaded_signature_ = next_signature;
    last_known_good_config_ = config_;
    last_known_good_version_ = loaded_config_version_;
    last_known_good_source_ = loaded_config_source_;
    last_known_good_signature_ = next_signature;
    has_committed_config_ = true;
    reload_success_count_.fetch_add(1U, std::memory_order_relaxed);
    last_reload_timestamp_ms_.store(NowUnixMs(), std::memory_order_relaxed);
    last_reload_status_code_.store(static_cast<std::int32_t>(vr::core::ErrorCode::kOk),
                                   std::memory_order_relaxed);
    reload_rollback_reason_code_.store(0, std::memory_order_relaxed);
    last_reload_audit_summary_ = audit.str() + ",status=ok";
    RefreshAggregatedMetrics(true);
    return vr::core::ErrorCode::kOk;
}

// 背压发送
vr::core::ErrorCode InterconnectBridge::PublishWithBackpressure(ITransport* const transport,
                                                                const std::string& encoded,
                                                                const std::uint32_t priority,
                                                                const BridgeSlaPolicy& policy) noexcept {
    auto send_once = [&]() -> vr::core::ErrorCode {
        return transport->SendWithTimeout(encoded, priority, policy.transport_send_timeout_ms);
    };

    vr::core::ErrorCode send_ec = send_once();
    if (send_ec == vr::core::ErrorCode::kOk) {
        return send_ec;
    }

    if (policy.drop_policy == DropPolicy::kDropNew) {
        return send_ec;
    }

    if (policy.drop_policy == DropPolicy::kDropOldest ||
        policy.backpressure_policy == BackpressurePolicy::kDropOldest) {
        if (send_ec == vr::core::ErrorCode::kTimeout ||
            send_ec == vr::core::ErrorCode::kThreadQueueFull ||
            send_ec == vr::core::ErrorCode::kWouldBlock) {
            const TransportCapabilities caps = transport->Caps();
            if (caps.supports_discard_oldest) {
                const vr::core::ErrorCode discard_ec = transport->DiscardOldest();
                if (discard_ec == vr::core::ErrorCode::kOk) {
                    backpressure_drop_count_.fetch_add(1U, std::memory_order_relaxed);
                    send_ec = send_once();
                }
            }
        }
    }

    if (policy.retry_budget.max_retries.value_or(0) > 0 &&
        policy.retry_budget.initial_backoff_ms > 0 &&
        policy.retry_budget.max_backoff_ms >= policy.retry_budget.initial_backoff_ms &&
        send_ec == vr::core::ErrorCode::kWouldBlock) {
        vr::core::RetryOptions options;
        options.max_retries = policy.retry_budget.max_retries.value_or(0);
        options.initial_backoff_ms = policy.retry_budget.initial_backoff_ms;
        options.max_backoff_ms = policy.retry_budget.max_backoff_ms;
        send_ec = vr::core::RetryPolicy::RetryOnWouldBlock(send_once, options);
    }

    return send_ec;
}

// 发送消息
bool InterconnectBridge::ShouldApplyCanaryForEnvelope(const MessageEnvelope& envelope) const {
    if (!config_.enable_config_canary) {
        return true;
    }

    if (!config_.config_canary_topic_prefix.empty()) {
        if (envelope.topic.rfind(config_.config_canary_topic_prefix, 0) != 0) {
            return true;
        }
    }

    if (config_.config_canary_channel >= 0) {
        if (static_cast<std::int32_t>(envelope.channel) != config_.config_canary_channel) {
            return true;
        }
    }

    const std::uint32_t gate = static_cast<std::uint32_t>(NowUnixMs() % 100U);
    return gate < config_.config_canary_percent;
}

// 判断当前消息是否启用幂等去重：仅对配置白名单 topic 生效。
bool InterconnectBridge::ShouldApplyIdempotency(const MessageEnvelope& envelope) const {
    if (config_.idempotency_topics.empty()) {
        return false;
    }

    for (const auto& topic : config_.idempotency_topics) {
        if (envelope.topic == topic) {
            return true;
        }
    }
    return false;
}

// 在滑动窗口内判定重复消息：优先使用显式 idempotency_key，缺省退化到 topic+source+sequence。
bool InterconnectBridge::IsDuplicateWithinWindow(const MessageEnvelope& envelope) {
    const std::string key = envelope.idempotency_key.empty()
        ? (envelope.topic + "|" + envelope.source + "|" + std::to_string(envelope.sequence))
        : envelope.idempotency_key;

    std::lock_guard<std::mutex> lock(idempotency_mutex_);
    auto& bucket = idempotency_recent_keys_[envelope.topic];
    for (const auto& existing : bucket) {
        if (existing == key) {
            return true;
        }
    }

    bucket.push_back(key);
    const std::size_t limit = std::max<std::size_t>(1U, config_.idempotency_window_size);
    while (bucket.size() > limit) {
        bucket.pop_front();
    }
    return false;
}


// 发布主路径（消息发送关键链路）
// 顺序：运行态校验 -> envelope 合法性/鉴权 -> canary/幂等判定 -> 协议编码 -> 策略发送 -> failover。
// 任何失败都需要更新对应指标，并尽可能保留可诊断上下文。
vr::core::ErrorCode InterconnectBridge::Publish(ITransport* const transport,
                                                const MessageEnvelope& envelope) noexcept {
    if (!running_.load(std::memory_order_acquire) || transport == nullptr) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kThreadStartFailed;
    }

    if (!IsEnvelopeValidForPublish(envelope)) {
        invalid_envelope_count_.fetch_add(1U, std::memory_order_relaxed);
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kInterconnectInvalidEnvelope;
    }

    if (authenticator_ && !authenticator_->Validate(envelope)) {
        invalid_envelope_count_.fetch_add(1U, std::memory_order_relaxed);
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kInterconnectInvalidEnvelope;
    }

    if (!ShouldApplyCanaryForEnvelope(envelope)) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        last_reload_audit_summary_ = "reload_audit:{status=canary_skip,reason=segment_gate}";
        diagnostics_manager_.RecordSnapshot(config_.diagnostics_snapshot_path,
                                           config_.diagnostics_snapshot_limit,
                                           "canary_skip",
                                           &envelope);
        diagnostic_snapshot_write_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kWouldBlock;
    }

    if (ShouldApplyIdempotency(envelope) && IsDuplicateWithinWindow(envelope)) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        idempotency_drop_count_.fetch_add(1U, std::memory_order_relaxed);
        diagnostics_manager_.RecordSnapshot(config_.diagnostics_snapshot_path,
                                           config_.diagnostics_snapshot_limit,
                                           "idempotency_drop",
                                           &envelope);
        diagnostic_snapshot_write_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kWouldBlock;
    }

    std::string encoded;
    const vr::core::ErrorCode encode_ec = protocol_manager_.Encode(
        protocol_manager_.ResolveMode(config_, envelope), envelope, &encoded);
    if (encode_ec != vr::core::ErrorCode::kOk) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        encode_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return encode_ec;
    }

    const BridgeSlaPolicy& policy = ResolvePolicy(envelope);
    const std::uint32_t priority = ResolvePriorityForPublish(envelope, transport);
    const bool flow_ok = AcquireFlowSlot(transport);
    if (!flow_ok) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        backpressure_drop_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kWouldBlock;
    }

    const vr::core::ErrorCode send_ec =
        PublishWithBackpressure(transport, encoded, priority, policy);

    ReleaseFlowSlot(transport);

    if (send_ec != vr::core::ErrorCode::kOk) {
        transport_orchestrator_.OnPrimarySendFailure();
        // failover 判定：仅在主链路发送失败且对应方向启用 failover 时尝试备用链路。
        // 注意：备用链路成功后需同步更新健康状态与 failover 事件计数。
        ITransport* failover_transport = transport_orchestrator_.ResolveFailover(
            transport,
            vehicle_to_robot_transport_.get(),
            robot_to_vehicle_transport_.get(),
            failover_from_vehicle_to_robot_enabled_,
            failover_from_robot_to_vehicle_enabled_);

        if (failover_transport != nullptr) {
            const vr::core::ErrorCode failover_ec =
                PublishWithBackpressure(failover_transport, encoded, priority, policy);
            if (failover_ec == vr::core::ErrorCode::kOk) {
                failover_hit_count_.fetch_add(1U, std::memory_order_relaxed);
                diag_failover_event_count_.fetch_add(1U, std::memory_order_relaxed);
                transport_primary_healthy_.store(0U, std::memory_order_relaxed);
                transport_secondary_healthy_.store(1U, std::memory_order_relaxed);
                tx_count_.fetch_add(1U, std::memory_order_relaxed);
                RefreshAggregatedMetrics();
                return vr::core::ErrorCode::kOk;
            }
        }

        transport_primary_healthy_.store(0U, std::memory_order_relaxed);
        transport_secondary_healthy_.store(0U, std::memory_order_relaxed);
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        transport_error_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return send_ec;
    }

    transport_orchestrator_.OnPrimarySendSuccess();
    transport_primary_healthy_.store(1U, std::memory_order_relaxed);
    transport_secondary_healthy_.store(1U, std::memory_order_relaxed);
    tx_count_.fetch_add(1U, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(metric_map_mutex_);
        topic_tx_count_[envelope.topic] += 1U;
        channel_tx_count_[envelope.channel] += 1U;
        qos_tx_count_[envelope.qos] += 1U;
    }
    RefreshAggregatedMetrics();
    return vr::core::ErrorCode::kOk;
}

// 车辆到机器人接收路径
void InterconnectBridge::VehicleInboundLoop() noexcept {
    ProcessInbound(vehicle_to_robot_transport_.get(), &robot_router_, "vehicle_to_robot");
}

const BridgeSlaPolicy& InterconnectBridge::ResolvePolicy(
    const MessageEnvelope& envelope) const noexcept {
    const PolicyCacheEntry* cached_entry = LookupPolicyCache(envelope);
    if (cached_entry != nullptr) {
        policy_cache_hit_count_.fetch_add(1U, std::memory_order_relaxed);
        if (cached_entry->override_applied) {
            policy_override_count_.fetch_add(1U, std::memory_order_relaxed);
        }
        if (cached_entry->conflict_detected) {
            policy_conflict_count_.fetch_add(1U, std::memory_order_relaxed);
            const std::uint64_t now_ms = NowUnixMs();
            if (NextLogAllowedMs(&last_policy_conflict_log_ms_, now_ms,
                                 kPolicyConflictSampleIntervalMs) == now_ms) {
                policy_conflict_sampled_count_.fetch_add(1U, std::memory_order_relaxed);
                LOG_WARN("Bridge policy conflict detected topic: " + envelope.topic);
            }
        }

        policy_hit_count_.fetch_add(1U, std::memory_order_relaxed);
        return cached_entry->policy;
    }

    policy_cache_miss_count_.fetch_add(1U, std::memory_order_relaxed);

    bool override_applied = false;
    bool conflict_detected = false;
    const BridgeSlaPolicy& policy = ResolvePolicyInternal(envelope, &override_applied,
                                                         &conflict_detected);
    const PolicyCacheEntry* stored_entry =
        StorePolicyCache(envelope, policy, override_applied, conflict_detected);

    if (override_applied) {
        policy_override_count_.fetch_add(1U, std::memory_order_relaxed);
    }
    if (conflict_detected) {
        policy_conflict_count_.fetch_add(1U, std::memory_order_relaxed);
        const std::uint64_t now_ms = NowUnixMs();
        if (NextLogAllowedMs(&last_policy_conflict_log_ms_, now_ms,
                             kPolicyConflictSampleIntervalMs) == now_ms) {
            policy_conflict_sampled_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge policy conflict detected topic: " + envelope.topic);
        }
    }

    policy_hit_count_.fetch_add(1U, std::memory_order_relaxed);
    if (stored_entry != nullptr) {
        return stored_entry->policy;
    }

    return policy;
}

void InterconnectBridge::ApplyTransportTuning(const BridgeConfig& config) {
    flow_limit_vehicle_ = config.vehicle_to_robot_endpoint.flow_limit_inflight;
    flow_limit_robot_ = config.robot_to_vehicle_endpoint.flow_limit_inflight;
    high_priority_threshold_vehicle_ = config.vehicle_to_robot_endpoint.high_priority_threshold;
    high_priority_threshold_robot_ = config.robot_to_vehicle_endpoint.high_priority_threshold;
    failover_from_vehicle_to_robot_enabled_ = !config.additional_endpoints.empty();
    failover_from_robot_to_vehicle_enabled_ = !config.additional_endpoints.empty();
    inflight_vehicle_.store(0U, std::memory_order_relaxed);
    inflight_robot_.store(0U, std::memory_order_relaxed);
}

std::uint32_t InterconnectBridge::ResolvePriorityForPublish(
    const MessageEnvelope& envelope,
    const ITransport* const transport) const noexcept {
    if (transport == vehicle_to_robot_transport_.get()) {
        const auto channel_index = static_cast<std::uint32_t>(envelope.channel);
        const auto qos_index = static_cast<std::uint32_t>(envelope.qos);
        if (channel_index >= high_priority_threshold_vehicle_ ||
            qos_index >= high_priority_threshold_vehicle_) {
            return config_.receive_priority + 1U;
        }
    } else if (transport == robot_to_vehicle_transport_.get()) {
        const auto channel_index = static_cast<std::uint32_t>(envelope.channel);
        const auto qos_index = static_cast<std::uint32_t>(envelope.qos);
        if (channel_index >= high_priority_threshold_robot_ ||
            qos_index >= high_priority_threshold_robot_) {
            return config_.receive_priority + 1U;
        }
    }

    return config_.receive_priority;
}

bool InterconnectBridge::AcquireFlowSlot(const ITransport* const transport) noexcept {
    if (transport == vehicle_to_robot_transport_.get()) {
        if (flow_limit_vehicle_ == 0U) {
            return true;
        }
        const auto current = inflight_vehicle_.fetch_add(1U, std::memory_order_relaxed) + 1U;
        if (current > flow_limit_vehicle_) {
            inflight_vehicle_.fetch_sub(1U, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    if (transport == robot_to_vehicle_transport_.get()) {
        if (flow_limit_robot_ == 0U) {
            return true;
        }
        const auto current = inflight_robot_.fetch_add(1U, std::memory_order_relaxed) + 1U;
        if (current > flow_limit_robot_) {
            inflight_robot_.fetch_sub(1U, std::memory_order_relaxed);
            return false;
        }
        return true;
    }

    return true;
}

void InterconnectBridge::ReleaseFlowSlot(const ITransport* const transport) noexcept {
    if (transport == vehicle_to_robot_transport_.get()) {
        if (flow_limit_vehicle_ == 0U) {
            return;
        }
        inflight_vehicle_.fetch_sub(1U, std::memory_order_relaxed);
        return;
    }

    if (transport == robot_to_vehicle_transport_.get()) {
        if (flow_limit_robot_ == 0U) {
            return;
        }
        inflight_robot_.fetch_sub(1U, std::memory_order_relaxed);
    }
}

void InterconnectBridge::RobotInboundLoop() noexcept {
    ProcessInbound(robot_to_vehicle_transport_.get(), &vehicle_router_, "robot_to_vehicle");
}

// 处理接收路径
void InterconnectBridge::ProcessInbound(ITransport* const transport, MessageRouter* const router,
                                        const std::string& loop_name) noexcept {
    while (running_.load(std::memory_order_acquire)) {
        std::string text;
        std::uint32_t priority = 0U;

        const std::int64_t receive_timeout_ms =
            config_.policy_table.default_policy.transport_receive_timeout_ms;
        const vr::core::ErrorCode recv_ec =
            transport->ReceiveWithTimeout(&text, &priority, receive_timeout_ms);
        if (recv_ec == vr::core::ErrorCode::kTimeout || recv_ec == vr::core::ErrorCode::kWouldBlock) {
            if (config_.policy_table.default_policy.enable_timeout_sleep) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    config_.policy_table.default_policy.receive_timeout_sleep_ms));
            }
            continue;
        }

        if (recv_ec != vr::core::ErrorCode::kOk) {
            transport_error_count_.fetch_add(1U, std::memory_order_relaxed);
            const std::uint64_t now_ms = NowUnixMs();
            if (NextLogAllowedMs(&last_receive_error_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                now_ms) {
            LOG_WARN("Bridge receive failed in " + loop_name);
            }
            if (config_.policy_table.default_policy.enable_timeout_sleep) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    config_.policy_table.default_policy.receive_timeout_sleep_ms));
            }
            RefreshAggregatedMetrics();
            continue;
        }

        rx_count_.fetch_add(1U, std::memory_order_relaxed);

        MessageEnvelope envelope;
        const vr::core::ErrorCode decode_ec = DecodeByProtocol(protocol_mode_, text, &envelope);
        if (decode_ec != vr::core::ErrorCode::kOk) {
            decode_fail_count_.fetch_add(1U, std::memory_order_relaxed);
            const std::uint64_t now_ms = NowUnixMs();
            if (NextLogAllowedMs(&last_decode_fail_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                now_ms) {
            LOG_WARN("Bridge decode failed in " + loop_name);
            }
            RefreshAggregatedMetrics();
            continue;
        }

        if (!IsEnvelopeValidForPublish(envelope)) {
            invalid_envelope_count_.fetch_add(1U, std::memory_order_relaxed);
            const std::uint64_t now_ms = NowUnixMs();
            if (NextLogAllowedMs(&last_invalid_envelope_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                now_ms) {
            LOG_WARN("Bridge invalid envelope in " + loop_name);
            }
            RefreshAggregatedMetrics();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(metric_map_mutex_);
            topic_rx_count_[envelope.topic] += 1U;
            channel_rx_count_[envelope.channel] += 1U;
            qos_rx_count_[envelope.qos] += 1U;
        }

        if (!envelope.trace_id.empty()) {
            trace_id_present_count_.fetch_add(1U, std::memory_order_relaxed);
            const std::uint64_t now_ms = NowUnixMs();
            const std::uint64_t sample_gate = NextLogAllowedMs(
                &last_trace_id_sample_ms_, now_ms, trace_sample_interval_ms_);
            if (sample_gate == now_ms) {
                trace_sample_counter_ = (trace_sample_counter_ + 1U) % 100U;
                if (trace_sample_counter_ < trace_sample_rate_percent_) {
                    trace_id_sampled_count_.fetch_add(1U, std::memory_order_relaxed);
                }
            }

            {
                std::lock_guard<std::mutex> lock(trace_index_mutex_);
                const auto it = trace_index_last_seen_ms_.find(envelope.trace_id);
                if (it == trace_index_last_seen_ms_.end()) {
                    trace_index_miss_count_.fetch_add(1U, std::memory_order_relaxed);
                } else {
                    trace_index_hit_count_.fetch_add(1U, std::memory_order_relaxed);
                }
                const bool is_new_trace = (it == trace_index_last_seen_ms_.end());
                trace_index_last_seen_ms_[envelope.trace_id] = now_ms;
                if (is_new_trace) {
                    trace_index_insert_order_.push_back(envelope.trace_id);
                }
                while (trace_index_insert_order_.size() > kTraceIndexMaxEntries) {
                    const std::string evict_key = trace_index_insert_order_.front();
                    trace_index_insert_order_.pop_front();
                    trace_index_last_seen_ms_.erase(evict_key);
                }
            }
            trace_linked_event_count_.fetch_add(1U, std::memory_order_relaxed);
        } else {
            trace_id_missing_count_.fetch_add(1U, std::memory_order_relaxed);
        }

        const BridgeSlaPolicy& policy = ResolvePolicy(envelope);
        const std::uint64_t now_ms = NowUnixMs();
        if (IsExpired(envelope, now_ms, policy.max_end_to_end_latency_ms)) {
            expired_drop_count_.fetch_add(1U, std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(metric_map_mutex_);
                topic_expired_drop_count_[envelope.topic] += 1U;
                channel_expired_drop_count_[envelope.channel] += 1U;
                qos_expired_drop_count_[envelope.qos] += 1U;
            }
            if (NextLogAllowedMs(&last_expired_log_ms_, now_ms, kLogThrottleIntervalMs) == now_ms) {
                LOG_WARN("Bridge dropped expired message trace_id=" + envelope.trace_id +
                         " topic=" + envelope.topic);
            }
            if (NextLogAllowedMs(&last_sla_violation_log_ms_, now_ms,
                                  sla_violation_sample_interval_ms_) == now_ms) {
                sla_violation_sampled_count_.fetch_add(1U, std::memory_order_relaxed);
            }
            RefreshAggregatedMetrics();
            continue;
        }

        const RouteResult route_result = router->Route(envelope);
        if (route_result != RouteResult::kOk) {
            if (route_result == RouteResult::kNoHandler) {
                route_miss_count_.fetch_add(1U, std::memory_order_relaxed);
                diag_route_event_count_.fetch_add(1U, std::memory_order_relaxed);
                if (NextLogAllowedMs(&last_route_miss_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                    now_ms) {
                    LOG_WARN("Bridge route missed trace_id=" + envelope.trace_id +
                             " topic=" + envelope.topic);
                }
            } else {
                handler_error_count_.fetch_add(1U, std::memory_order_relaxed);
                diag_route_event_count_.fetch_add(1U, std::memory_order_relaxed);
                if (NextLogAllowedMs(&last_handler_error_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                    now_ms) {
                    LOG_ERROR_CODE(vr::core::ErrorCode::kInterconnectRouteHandlerError,
                                   "Bridge handler exception trace_id=" + envelope.trace_id +
                                   " topic=" + envelope.topic);
                }
            }
            RefreshAggregatedMetrics();
            continue;
        }

        RefreshAggregatedMetrics();
        if (enable_diagnostics_reporting_ && diagnostics_reporter_ != nullptr) {
            const std::uint64_t now_ms = NowUnixMs();
            if (now_ms - last_diagnostics_report_ms_ >= diagnostics_report_interval_ms_) {
                diagnostics_reporter_->Submit(metrics_aggregator_.ExportJsonLightweight());
                last_diagnostics_report_ms_ = now_ms;
            }
        }
    }
}

// 是否需要刷新聚合指标
bool InterconnectBridge::ShouldRefreshMetrics(const std::uint64_t now_ms) const noexcept {
    const std::uint64_t last = last_metrics_refresh_ms_.load(std::memory_order_relaxed);
    if (now_ms < last) {
        return true;
    }
    return (now_ms - last) >= kMetricsRefreshIntervalMs;
}

// 刷新聚合指标（时机控制关键点）
// 1) force=true：用于关键路径（如配置提交/回滚、快照导出前）强制刷新，保证观测一致性；
// 2) force=false：按节流周期刷新，避免高频路径下指标聚合造成额外开销。
void InterconnectBridge::RefreshAggregatedMetrics(const bool force) const noexcept {
    const std::uint64_t now_ms = NowUnixMs();
    if (!force && !ShouldRefreshMetrics(now_ms)) {
        return;
    }

    last_metrics_refresh_ms_.store(now_ms, std::memory_order_relaxed);

    // 统计指标
    BridgeMetrics bridge_metrics;
    bridge_metrics.tx_count = tx_count_.load(std::memory_order_relaxed);
    bridge_metrics.tx_fail_count = tx_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.rx_count = rx_count_.load(std::memory_order_relaxed);
    bridge_metrics.encode_fail_count = encode_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.decode_fail_count = decode_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.expired_drop_count = expired_drop_count_.load(std::memory_order_relaxed);
    bridge_metrics.route_miss_count = route_miss_count_.load(std::memory_order_relaxed);
    bridge_metrics.handler_error_count = handler_error_count_.load(std::memory_order_relaxed);
    bridge_metrics.invalid_envelope_count = invalid_envelope_count_.load(std::memory_order_relaxed);
    bridge_metrics.transport_error_count = transport_error_count_.load(std::memory_order_relaxed);
    bridge_metrics.backpressure_drop_count = backpressure_drop_count_.load(std::memory_order_relaxed);
    bridge_metrics.idempotency_drop_count = idempotency_drop_count_.load(std::memory_order_relaxed);
    bridge_metrics.diagnostic_snapshot_write_count =
        diagnostic_snapshot_write_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_hit_count = policy_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_override_count = policy_override_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_conflict_count = policy_conflict_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_conflict_sampled_count =
        policy_conflict_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_cache_hit_count = policy_cache_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_cache_miss_count =
        policy_cache_miss_count_.load(std::memory_order_relaxed);

    const auto lint_counts = GetPolicyLintCounts();
    bridge_metrics.policy_lint_issue_count = lint_counts.first;
    bridge_metrics.policy_lint_warning_count = lint_counts.second;

    bridge_metrics.trace_id_present_count =
        trace_id_present_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_id_missing_count =
        trace_id_missing_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_id_sampled_count =
        trace_id_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_index_hit_count =
        trace_index_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_index_miss_count =
        trace_index_miss_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_linked_event_count =
        trace_linked_event_count_.load(std::memory_order_relaxed);
    bridge_metrics.sla_violation_sampled_count =
        sla_violation_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.reload_success_count = reload_success_count_.load(std::memory_order_relaxed);
    bridge_metrics.reload_fail_count = reload_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.reload_rollback_count = reload_rollback_count_.load(std::memory_order_relaxed);
    bridge_metrics.last_reload_timestamp_ms =
        last_reload_timestamp_ms_.load(std::memory_order_relaxed);
    bridge_metrics.last_reload_status_code =
        last_reload_status_code_.load(std::memory_order_relaxed);
    bridge_metrics.reload_rollback_reason_code =
        reload_rollback_reason_code_.load(std::memory_order_relaxed);
    bridge_metrics.loaded_config_version = loaded_config_version_;

    bridge_metrics.protocol_mode_code = static_cast<std::uint32_t>(protocol_mode_);
    const auto cap = GetProtocolCapabilitySnapshot(now_ms);
    bridge_metrics.protocol_cap_legacy = cap.legacy;
    bridge_metrics.protocol_cap_compact = cap.compact;
    bridge_metrics.protocol_cap_protobuf = cap.protobuf;
    bridge_metrics.protocol_cap_cbor = cap.cbor;
    bridge_metrics.configured_transport_pool_size =
        static_cast<std::uint64_t>(2U + config_.additional_endpoints.size());
    bridge_metrics.failover_hit_count = failover_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.transport_primary_healthy =
        transport_primary_healthy_.load(std::memory_order_relaxed);
    bridge_metrics.transport_secondary_healthy =
        transport_secondary_healthy_.load(std::memory_order_relaxed);

    bridge_metrics.diag_dump_state_count =
        diag_dump_state_count_.load(std::memory_order_relaxed);
    bridge_metrics.diag_route_event_count =
        diag_route_event_count_.load(std::memory_order_relaxed);
    bridge_metrics.diag_failover_event_count =
        diag_failover_event_count_.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(metric_map_mutex_);
        bridge_metrics.topic_tx_count = topic_tx_count_;
        bridge_metrics.topic_rx_count = topic_rx_count_;
        bridge_metrics.topic_expired_drop_count = topic_expired_drop_count_;
        bridge_metrics.channel_tx_count = channel_tx_count_;
        bridge_metrics.channel_rx_count = channel_rx_count_;
        bridge_metrics.channel_expired_drop_count = channel_expired_drop_count_;
        bridge_metrics.qos_tx_count = qos_tx_count_;
        bridge_metrics.qos_rx_count = qos_rx_count_;
        bridge_metrics.qos_expired_drop_count = qos_expired_drop_count_;
    }

    metrics_aggregator_.UpdateBridgeMetrics(bridge_metrics);
    metrics_aggregator_.UpdateThreadPoolMetrics(worker_pool_.GetMetrics());
}

}  // namespace interconnect
}  // namespace vr

