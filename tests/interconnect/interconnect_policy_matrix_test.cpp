#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/policy_manager.hpp"
#include "interconnect/transport.hpp"

namespace {

class BackpressureProbeTransport final : public vr::interconnect::ITransport {
public:
    explicit BackpressureProbeTransport(const bool fail_first) : fail_first_(fail_first) {}

    vr::core::ErrorCode Create(const vr::interconnect::TransportEndpointConfig& config) noexcept override {
        (void)config;
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode SendWithTimeout(const std::string& message, const std::uint32_t priority,
                                        const std::int64_t timeout_ms) noexcept override {
        (void)message;
        (void)priority;
        (void)timeout_ms;
        ++send_calls_;
        if (fail_first_ && send_calls_ == 1U) {
            return vr::core::ErrorCode::kWouldBlock;
        }
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           const std::int64_t timeout_ms) noexcept override {
        (void)message;
        (void)priority;
        (void)timeout_ms;
        return vr::core::ErrorCode::kTimeout;
    }

    vr::core::ErrorCode DiscardOldest() noexcept override {
        ++discard_calls_;
        return vr::core::ErrorCode::kOk;
    }

    // ITransport 要求实现 Name()/Caps()。
    const char* Name() const noexcept override { return "test.backpressure-probe"; }

    vr::interconnect::TransportCapabilities Caps() const noexcept override {
        vr::interconnect::TransportCapabilities caps;
        caps.supports_priority = true;
        caps.supports_discard_oldest = true;
        caps.supports_unlink = false;
        return caps;
    }

    void Close() noexcept override {}
    void Unlink() noexcept override {}

    std::uint32_t send_calls() const noexcept { return send_calls_; }
    std::uint32_t discard_calls() const noexcept { return discard_calls_; }

private:
    bool fail_first_{false};
    std::uint32_t send_calls_{0U};
    std::uint32_t discard_calls_{0U};
};

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

vr::interconnect::BridgeConfig MakePolicyConfig() {
    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "policy_v2r";
    config.robot_to_vehicle_endpoint.name = "policy_r2v";
    config.thread_pool.worker_count = 1;
    config.thread_pool.queue_capacity = 4;

    config.sla_policy.max_end_to_end_latency_ms = 100U;
    config.sla_policy.transport_receive_timeout_ms = 20;
    config.sla_policy.transport_send_timeout_ms = 20;
    config.sla_policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kReject;
    config.sla_policy.drop_policy = vr::interconnect::DropPolicy::kDropNone;
    config.sla_policy.retry_budget.max_retries = 1;
    config.sla_policy.retry_budget.initial_backoff_ms = 5;
    config.sla_policy.retry_budget.max_backoff_ms = 10;

    config.policy_table.default_policy.max_end_to_end_latency_ms = 200U;
    config.policy_table.default_policy.transport_receive_timeout_ms = 30;
    config.policy_table.default_policy.transport_send_timeout_ms = 30;
    config.policy_table.default_policy.backpressure_policy =
        vr::interconnect::BackpressurePolicy::kReject;
    config.policy_table.default_policy.drop_policy = vr::interconnect::DropPolicy::kDropNone;

    vr::interconnect::PolicyRule control_rule;
    control_rule.match_any_channel = false;
    control_rule.channel = vr::interconnect::ChannelType::kControl;
    control_rule.topic = "vehicle.command";
    control_rule.policy.max_end_to_end_latency_ms = 5U;
    control_rule.policy.transport_receive_timeout_ms = 7;
    control_rule.policy.transport_send_timeout_ms = 7;
    control_rule.policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kDropOldest;
    control_rule.policy.drop_policy = vr::interconnect::DropPolicy::kDropOldest;
    control_rule.policy.retry_budget.max_retries = 1;
    control_rule.policy.retry_budget.initial_backoff_ms = 5;
    control_rule.policy.retry_budget.max_backoff_ms = 10;
    config.policy_table.rules.push_back(control_rule);

    return config;
}

bool TestDropOldestPolicyApplied() {
    auto tx = std::make_unique<BackpressureProbeTransport>(true);
    auto rx = std::make_unique<BackpressureProbeTransport>(false);
    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakePolicyConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "vehicle.command";
    msg.channel = vr::interconnect::ChannelType::kControl;
    msg.trace_id = "policy_drop";
    msg.payload = "cmd";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    const auto snapshot = bridge.CaptureMetricsSnapshot();
    const auto lint_report = bridge.GetPolicyLintReport();
    const auto conflicts = bridge.DumpPolicyConflicts();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "drop-oldest policy should recover") &&
           ExpectTrue(tx_raw->discard_calls() >= 1U, "drop-oldest should discard") &&
           ExpectTrue(snapshot.bridge_metrics.backpressure_drop_count >= 1U,
                      "backpressure metric should increase") &&
           ExpectTrue(!lint_report.empty(), "lint report should be available") &&
           ExpectTrue(conflicts.empty(), "policy conflicts should be empty");
}

bool TestDefaultRejectPolicyApplied() {
    auto tx = std::make_unique<BackpressureProbeTransport>(true);
    auto rx = std::make_unique<BackpressureProbeTransport>(false);
    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakePolicyConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "unknown.topic";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.trace_id = "policy_reject";
    msg.payload = "cmd";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    const auto snapshot = bridge.CaptureMetricsSnapshot();
    const auto lint_report = bridge.GetPolicyLintReport();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kWouldBlock,
                      "default reject policy should not retry") &&
           ExpectTrue(tx_raw->discard_calls() == 0U, "reject policy should not discard") &&
           ExpectTrue(snapshot.bridge_metrics.backpressure_drop_count == 0U,
                      "backpressure metric should not increase") &&
           ExpectTrue(!lint_report.empty(), "lint report should be available");
}

// ===========================================================================
// 以下三条是 Bug 2 的防回归用例。
//
// 背景：`RetryBudget::max_retries` 原来是裸 int，归一化用 `<= 0` 判断"有没有配"，
// 于是显式配置的 0（明确表示不重试）被当成"没配"，被 sla_policy 的预算覆盖，
// 标称 kReject 的策略照样会重试一次并成功。现在改成 std::optional：
// 未配置（nullopt）才继承，显式配置（含 0）一律保留。
// ===========================================================================

// 显式配置 max_retries = 0 必须保留，不能被 sla_policy 的预算覆盖。
bool TestExplicitZeroRetryBudgetIsHonored() {
    auto config = MakePolicyConfig();                                // sla 预算 = 1/5/10
    config.policy_table.default_policy.retry_budget.max_retries = 0;  // 显式 0

    vr::interconnect::PolicyManager manager;
    manager.NormalizePolicyDefaults(&config);

    const auto& budget = config.policy_table.default_policy.retry_budget;
    return ExpectTrue(budget.max_retries.has_value(),
                      "explicit max_retries must remain configured") &&
           ExpectTrue(budget.max_retries.value() == 0,
                      "explicit max_retries=0 must not be overwritten by sla_policy");
}

// 反向保证：未配置时仍应继承 sla_policy 的预算，归一化没有被过度收紧。
bool TestUnsetRetryBudgetStillInheritsSla() {
    auto config = MakePolicyConfig();
    // kDropOldest 表示这条策略愿意缓冲/重试，此时才允许从 sla_policy 继承预算。
    config.policy_table.default_policy.backpressure_policy =
        vr::interconnect::BackpressurePolicy::kDropOldest;
    config.policy_table.default_policy.retry_budget = vr::interconnect::RetryBudget{};

    vr::interconnect::PolicyManager manager;
    manager.NormalizePolicyDefaults(&config);

    const auto& budget = config.policy_table.default_policy.retry_budget;
    return ExpectTrue(budget.max_retries.has_value() &&
                          budget.max_retries.value() ==
                              config.sla_policy.retry_budget.max_retries.value(),
                      "unset retry budget should still inherit sla_policy");
}

// 端到端：显式 max_retries = 0 且 backpressure = kReject 时，发布失败只发一次、不重试。
bool TestNoRetryWhenExplicitlyDisabled() {
    auto tx = std::make_unique<BackpressureProbeTransport>(true);  // 首次必定 kWouldBlock
    auto rx = std::make_unique<BackpressureProbeTransport>(false);
    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakePolicyConfig();
    config.policy_table.default_policy.retry_budget.max_retries = 0;
    config.policy_table.default_policy.backpressure_policy =
        vr::interconnect::BackpressurePolicy::kReject;
    config.policy_table.default_policy.drop_policy = vr::interconnect::DropPolicy::kDropNone;

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "unknown.topic";  // 不命中任何规则 -> 走 default_policy
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.trace_id = "explicit_zero_retry";
    msg.payload = "cmd";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kWouldBlock,
                      "explicit zero retry must fail fast") &&
           ExpectTrue(tx_raw->send_calls() == 1U,
                      "must not retry: exactly one send attempt");
}

// ===========================================================================
// 判重丢弃必须**可观测**。
//
// 背景：`idempotency_drop_count_` 曾经是"只写不读"的计数器 —— 声明 + 一处
// `fetch_add`，全库无任何读取方，既不进 BridgeMetrics 也不进 Prometheus/JSON。
// 于是"这条消息是不是被判重丢了"在运维侧**无法验证**，查不到就会去查别处
// （网络丢包 / 路由 miss / 对端没发），这是典型的归因错位。
// 本用例锁住：重复消息被判重后，`bridge_metrics.idempotency_drop_count` 必须为 1。
// ===========================================================================
bool TestIdempotencyDropIsObservable() {
    auto tx = std::make_unique<BackpressureProbeTransport>(false);  // 不注入发送故障
    auto rx = std::make_unique<BackpressureProbeTransport>(false);
    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));

    auto config = MakePolicyConfig();
    config.idempotency_topics = {"unknown.topic"};  // 只对这个 topic 做判重
    config.idempotency_window_size = 8U;

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "unknown.topic";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.sequence = 7U;
    msg.payload = "cmd";
    msg.timestamp_ms = static_cast<std::uint64_t>(
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now())
            .time_since_epoch()
            .count());

    const auto first = bridge.PublishFromVehicle(msg);
    const auto second = bridge.PublishFromVehicle(msg);  // 完全相同的第二条
    const auto snapshot = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(first == vr::core::ErrorCode::kOk, "first publish should succeed") &&
           ExpectTrue(second == vr::core::ErrorCode::kWouldBlock,
                      "duplicate publish should be dropped by idempotency window") &&
           ExpectTrue(snapshot.bridge_metrics.idempotency_drop_count == 1U,
                      "idempotency_drop_count must be observable (was write-only before)") &&
           // 判重丢弃时会顺手落一份诊断快照；这个计数器同样是"只写不读"，
           // 同批补导出后必须可见 —— 用来回答"快照到底写没写、写得频不频繁"。
           ExpectTrue(snapshot.bridge_metrics.diagnostic_snapshot_write_count == 1U,
                      "diagnostic_snapshot_write_count must be observable (was write-only before)");
}

}  // namespace

int main() {
    if (!TestDropOldestPolicyApplied()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }
    if (!TestDefaultRejectPolicyApplied()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }
    if (!TestExplicitZeroRetryBudgetIsHonored()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }
    if (!TestUnsetRetryBudgetStillInheritsSla()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }
    if (!TestNoRetryWhenExplicitlyDisabled()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }
    if (!TestIdempotencyDropIsObservable()) {
        std::cerr << "interconnect policy matrix test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect policy matrix test passed." << std::endl;
    return 0;
}
