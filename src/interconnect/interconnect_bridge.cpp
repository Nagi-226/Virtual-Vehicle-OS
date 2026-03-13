#include "interconnect/interconnect_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "interconnect/message_codec.hpp"
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
    if (config_.policy_table.rules.empty() &&
        config_.policy_table.default_policy.max_end_to_end_latency_ms == 100U &&
        config_.policy_table.default_policy.transport_receive_timeout_ms == 50 &&
        config_.policy_table.default_policy.transport_send_timeout_ms == 50 &&
        config_.policy_table.default_policy.backpressure_policy == BackpressurePolicy::kReject) {
        config_.policy_table.default_policy = config_.sla_policy;
    }
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
    PopulateDefaultTemplateRules();
    RebuildPolicyIndex();

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

std::string InterconnectBridge::ExportPrometheusMetrics() const {
    return metrics_aggregator_.ExportPrometheus();
}

std::string InterconnectBridge::ExportJsonMetrics() const {
    return metrics_aggregator_.ExportJson();
}

std::string InterconnectBridge::ExportJsonMetricsLightweight() const {
    return metrics_aggregator_.ExportJsonLightweight();
}

std::string InterconnectBridge::GetLoadedConfigSource() const {
    return loaded_config_source_;
}

void InterconnectBridge::SetDiagnosticsReporter(std::shared_ptr<IDiagnosticsReporter> reporter) {
    diagnostics_reporter_ = std::move(reporter);
}

std::uint64_t InterconnectBridge::GetLoadedConfigVersion() const noexcept {
    return loaded_config_version_;
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

    std::lock_guard<std::mutex> lock(policy_cache_mutex_);
    policy_cache_index_.clear();
    policy_cache_items_.clear();
}

void InterconnectBridge::PopulateDefaultTemplateRules() {
    if (!config_.policy_table.template_rules.empty()) {
        return;
    }

    PolicyRule control_rule;
    control_rule.priority = 10U;
    control_rule.match_any_channel = false;
    control_rule.channel = ChannelType::kControl;
    control_rule.topic = "*";
    control_rule.policy.max_end_to_end_latency_ms = 50U;
    control_rule.policy.backpressure_policy = BackpressurePolicy::kDropOldest;

    PolicyRule telemetry_rule;
    telemetry_rule.priority = 5U;
    telemetry_rule.match_any_channel = false;
    telemetry_rule.channel = ChannelType::kTelemetry;
    telemetry_rule.topic = "*";
    telemetry_rule.policy.max_end_to_end_latency_ms = 150U;
    telemetry_rule.policy.backpressure_policy = BackpressurePolicy::kReject;

    config_.policy_table.template_rules.push_back(control_rule);
    config_.policy_table.template_rules.push_back(telemetry_rule);
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

const InterconnectBridge::PolicyCacheEntry* InterconnectBridge::LookupPolicyCache(
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

const InterconnectBridge::PolicyCacheEntry* InterconnectBridge::StorePolicyCache(
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

    if (override_policy != nullptr) {
        *out_override_applied = true;
        return *override_policy;
    }

    if (rule_policy != nullptr) {
        return *rule_policy;
    }

    if (template_policy != nullptr) {
        return *template_policy;
    }

    return config_.policy_table.default_policy;
}

// 重新加载配置
vr::core::ErrorCode InterconnectBridge::ReloadConfigIfChanged(
    IConfigProvider* const provider) noexcept {
    if (provider == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    const vr::core::ErrorCode reload_ec = provider->Reload();
    if (reload_ec != vr::core::ErrorCode::kOk) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        return reload_ec;
    }

    const std::uint64_t new_version = provider->GetVersion();
    if (new_version == loaded_config_version_) {
        return vr::core::ErrorCode::kOk;
    }

    BridgeConfig cfg;
    std::string source;
    const vr::core::ErrorCode load_ec = provider->LoadBridgeConfig(&cfg, &source);
    if (load_ec != vr::core::ErrorCode::kOk) {
        reload_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        return load_ec;
    }

    config_ = cfg;
    if (config_.policy_table.rules.empty() &&
        config_.policy_table.default_policy.max_end_to_end_latency_ms == 100U &&
        config_.policy_table.default_policy.transport_receive_timeout_ms == 50 &&
        config_.policy_table.default_policy.transport_send_timeout_ms == 50 &&
        config_.policy_table.default_policy.backpressure_policy == BackpressurePolicy::kReject) {
        config_.policy_table.default_policy = config_.sla_policy;
    }
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
    PopulateDefaultTemplateRules();
    RebuildPolicyIndex();
    loaded_config_source_ = source;
    loaded_config_version_ = new_version;
    reload_success_count_.fetch_add(1U, std::memory_order_relaxed);
    last_reload_timestamp_ms_.store(NowUnixMs(), std::memory_order_relaxed);
    RefreshAggregatedMetrics(true);
    return vr::core::ErrorCode::kOk;
}

// 背压发送
vr::core::ErrorCode InterconnectBridge::PublishWithBackpressure(ITransport* const transport,
                                                                const std::string& encoded,
                                                                const std::uint32_t priority,
                                                                const BridgeSlaPolicy& policy) noexcept {
    vr::core::ErrorCode send_ec =
        transport->SendWithTimeout(encoded, priority, policy.transport_send_timeout_ms);
    if (send_ec == vr::core::ErrorCode::kOk) {
        return send_ec;
    }

    if (policy.backpressure_policy == BackpressurePolicy::kDropOldest &&
        (send_ec == vr::core::ErrorCode::kTimeout || send_ec == vr::core::ErrorCode::kThreadQueueFull ||
         send_ec == vr::core::ErrorCode::kWouldBlock)) {
        const vr::core::ErrorCode discard_ec = transport->DiscardOldest();
        if (discard_ec == vr::core::ErrorCode::kOk) {
            backpressure_drop_count_.fetch_add(1U, std::memory_order_relaxed);
            send_ec = transport->SendWithTimeout(encoded, priority, policy.transport_send_timeout_ms);
        }
    }

    return send_ec;
}

// 发送消息
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

    std::string encoded;
    const vr::core::ErrorCode encode_ec = MessageCodec::Encode(envelope, &encoded);
    if (encode_ec != vr::core::ErrorCode::kOk) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        encode_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return encode_ec;
    }

    const BridgeSlaPolicy& policy = ResolvePolicy(envelope);
    const vr::core::ErrorCode send_ec =
        PublishWithBackpressure(transport, encoded, config_.receive_priority, policy);

    if (send_ec != vr::core::ErrorCode::kOk) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        transport_error_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return send_ec;
    }

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

void InterconnectBridge::PopulateDefaultTemplateRules() {
    if (!config_.policy_table.template_rules.empty()) {
        return;
    }

    PolicyRule control_rule;
    control_rule.priority = 10U;
    control_rule.match_any_channel = false;
    control_rule.channel = ChannelType::kControl;
    control_rule.topic = "*";
    control_rule.policy.max_end_to_end_latency_ms = 50U;
    control_rule.policy.backpressure_policy = BackpressurePolicy::kDropOldest;

    PolicyRule telemetry_rule;
    telemetry_rule.priority = 5U;
    telemetry_rule.match_any_channel = false;
    telemetry_rule.channel = ChannelType::kTelemetry;
    telemetry_rule.topic = "*";
    telemetry_rule.policy.max_end_to_end_latency_ms = 150U;
    telemetry_rule.policy.backpressure_policy = BackpressurePolicy::kReject;

    config_.policy_table.template_rules.push_back(control_rule);
    config_.policy_table.template_rules.push_back(telemetry_rule);
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
        const vr::core::ErrorCode decode_ec = MessageCodec::Decode(text, &envelope);
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
                LOG_WARN("Bridge dropped expired message topic: " + envelope.topic);
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
                if (NextLogAllowedMs(&last_route_miss_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                    now_ms) {
                    LOG_WARN("Bridge route missed topic: " + envelope.topic);
                }
            } else {
                handler_error_count_.fetch_add(1U, std::memory_order_relaxed);
                if (NextLogAllowedMs(&last_handler_error_log_ms_, now_ms, kLogThrottleIntervalMs) ==
                    now_ms) {
                    LOG_ERROR_CODE(vr::core::ErrorCode::kInterconnectRouteHandlerError,
                                   "Bridge handler exception topic: " + envelope.topic);
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
bool InterconnectBridge::ShouldRefreshMetrics(const std::uint64_t now_ms) noexcept {
    const std::uint64_t last = last_metrics_refresh_ms_.load(std::memory_order_relaxed);
    if (now_ms < last) {
        return true;
    }
    return (now_ms - last) >= kMetricsRefreshIntervalMs;
}

// 刷新聚合指标
void InterconnectBridge::RefreshAggregatedMetrics(const bool force) noexcept {
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
    bridge_metrics.policy_hit_count = policy_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_override_count = policy_override_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_conflict_count = policy_conflict_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_conflict_sampled_count =
        policy_conflict_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_cache_hit_count = policy_cache_hit_count_.load(std::memory_order_relaxed);
    bridge_metrics.policy_cache_miss_count =
        policy_cache_miss_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_id_present_count =
        trace_id_present_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_id_missing_count =
        trace_id_missing_count_.load(std::memory_order_relaxed);
    bridge_metrics.trace_id_sampled_count =
        trace_id_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.sla_violation_sampled_count =
        sla_violation_sampled_count_.load(std::memory_order_relaxed);
    bridge_metrics.reload_success_count = reload_success_count_.load(std::memory_order_relaxed);
    bridge_metrics.reload_fail_count = reload_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.last_reload_timestamp_ms =
        last_reload_timestamp_ms_.load(std::memory_order_relaxed);
    bridge_metrics.loaded_config_version = loaded_config_version_;

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

