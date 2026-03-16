#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
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

    std::cout << "interconnect policy matrix test passed." << std::endl;
    return 0;
}
