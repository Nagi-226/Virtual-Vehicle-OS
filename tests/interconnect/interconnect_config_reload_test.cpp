#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/static_config_provider.hpp"
#include "interconnect/transport.hpp"

namespace {

class NoopTransport final : public vr::interconnect::ITransport {
public:
    vr::core::ErrorCode Create(const vr::interconnect::TransportEndpointConfig& config) noexcept override {
        (void)config;
        return vr::core::ErrorCode::kOk;
    }
    vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                        std::int64_t timeout_ms) noexcept override {
        last_message_ = message;
        (void)priority;
        (void)timeout_ms;
        return vr::core::ErrorCode::kOk;
    }
    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           std::int64_t timeout_ms) noexcept override {
        (void)message;
        (void)priority;
        (void)timeout_ms;
        return vr::core::ErrorCode::kTimeout;
    }
    vr::core::ErrorCode DiscardOldest() noexcept override { return vr::core::ErrorCode::kOk; }
    // ITransport 要求实现 Name()/Caps()。
    const char* Name() const noexcept override { return "test.noop"; }

    vr::interconnect::TransportCapabilities Caps() const noexcept override {
        vr::interconnect::TransportCapabilities caps;
        caps.supports_priority = false;
        caps.supports_discard_oldest = false;
        caps.supports_unlink = false;
        return caps;
    }

    // SendWithTimeout() 会把报文记在这里，供用例做"报文内容是否被改写"的核对。
    const std::string& last_message() const noexcept { return last_message_; }

    void Close() noexcept override {}
    void Unlink() noexcept override {}

private:
    std::string last_message_;
};

bool ExpectTrue(const bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

vr::interconnect::BridgeConfig MakeValidConfig() {
    vr::interconnect::BridgeConfig cfg;
    cfg.vehicle_to_robot_endpoint.name = "reload_v2r";
    cfg.robot_to_vehicle_endpoint.name = "reload_r2v";
    cfg.thread_pool.worker_count = 1;
    cfg.thread_pool.queue_capacity = 4;
    cfg.policy_table.default_policy = cfg.sla_policy;
    cfg.sla_policy.retry_budget.max_retries = 1;
    cfg.sla_policy.retry_budget.initial_backoff_ms = 5;
    cfg.sla_policy.retry_budget.max_backoff_ms = 10;
    return cfg;
}

bool TestReloadRollbackOnInvalidConfig() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto base_config = MakeValidConfig();
    vr::interconnect::StaticConfigProvider provider(base_config, "static://base");

    if (!ExpectTrue(bridge.Start(&provider) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::BridgeConfig bad_config = base_config;
    bad_config.thread_pool.worker_count = 0;
    provider.UpdateConfigForTest(bad_config, "static://bad");

    const auto reload_ec = bridge.ReloadConfigIfChanged(&provider);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    const auto version = bridge.GetLoadedConfigVersion();
    const auto source = bridge.GetLoadedConfigSource();
    bridge.Stop();

    return ExpectTrue(reload_ec == vr::core::ErrorCode::kInvalidParam, "reload should fail") &&
           ExpectTrue(metrics.bridge_metrics.reload_rollback_count == 1U,
                      "rollback count should increment") &&
           ExpectTrue(metrics.bridge_metrics.last_reload_status_code != 0,
                      "reload status should be recorded") &&
           ExpectTrue(version == provider.GetVersion() - 1,
                      "rollback keeps previous version") &&
           ExpectTrue(source == "static://base", "rollback keeps previous source");
}

bool TestReloadRollbackOnPolicyLockViolation() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto base_config = MakeValidConfig();
    base_config.policy_table.default_policy.lock_policy = true;
    base_config.policy_table.default_policy.max_end_to_end_latency_ms = 120;
    vr::interconnect::StaticConfigProvider provider(base_config, "static://lock_base");

    if (!ExpectTrue(bridge.Start(&provider) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::BridgeConfig violating = base_config;
    violating.policy_table.default_policy.max_end_to_end_latency_ms = 200;
    provider.UpdateConfigForTest(violating, "static://lock_violation");

    const auto reload_ec = bridge.ReloadConfigIfChanged(&provider);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    const auto audit = bridge.GetLastReloadAuditSummary();
    bridge.Stop();

    return ExpectTrue(reload_ec == vr::core::ErrorCode::kInvalidParam,
                      "policy lock violation should fail reload") &&
           ExpectTrue(metrics.bridge_metrics.reload_rollback_reason_code == 5,
                      "rollback reason should be policy lock violation") &&
           ExpectTrue(audit.find("policy_lock_violation") != std::string::npos,
                      "audit should include policy lock violation");
}

bool TestHighRiskConfigAutoRollback() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto base = MakeValidConfig();
    base.enable_config_risk_guard = true;
    base.auto_rollback_on_high_risk = true;
    base.high_risk_block_threshold = 2U;

    vr::interconnect::StaticConfigProvider provider(base, "static://risk_base");
    if (!ExpectTrue(bridge.Start(&provider) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    auto risky = base;
    risky.protocol_mode = vr::interconnect::MessageProtocolMode::kProtobufReserved;
    risky.thread_pool.worker_count = 2U;
    provider.UpdateConfigForTest(risky, "static://risk_high");

    const auto ec = bridge.ReloadConfigIfChanged(&provider);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    const auto audit = bridge.GetLastReloadAuditSummary();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kInvalidParam,
                      "high risk config should auto rollback") &&
           ExpectTrue(metrics.bridge_metrics.reload_rollback_reason_code == 6,
                      "rollback reason should be high risk") &&
           ExpectTrue(audit.find("high_risk_config_change") != std::string::npos,
                      "audit should include high risk reason");
}

bool TestCanarySegmentGateByTopicChannel() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto config = MakeValidConfig();
    config.enable_config_canary = true;
    config.config_canary_percent = 0U;
    config.config_canary_topic_prefix = "canary.";
    config.config_canary_channel = static_cast<std::int32_t>(vr::interconnect::ChannelType::kTelemetry);

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope hit_msg;
    hit_msg.source = "vehicle";
    hit_msg.target = "robot";
    hit_msg.topic = "canary.topic.hit";
    hit_msg.channel = vr::interconnect::ChannelType::kTelemetry;
    hit_msg.payload = "payload";
    hit_msg.timestamp_ms = 1U;

    vr::interconnect::MessageEnvelope bypass_msg = hit_msg;
    bypass_msg.topic = "normal.topic.bypass";

    const auto hit_ec = bridge.PublishFromVehicle(hit_msg);
    const auto bypass_ec = bridge.PublishFromVehicle(bypass_msg);
    bridge.Stop();

    return ExpectTrue(hit_ec == vr::core::ErrorCode::kWouldBlock,
                      "canary hit message should be gated") &&
           ExpectTrue(bypass_ec == vr::core::ErrorCode::kOk,
                      "non canary segment should bypass gate");
}

// ===========================================================================
// canary 灰度门禁的两条防回归用例（Bug 1b）。
//
// 背景：canary 灰度的语义是"把新配置先给一小部分流量试"，前提是**已经有一份
// 可用旧配置在跑**。首启时不存在旧版本，灰度无从谈起；旧实现却在首启也走门禁，
// 于是 canary_percent = 0（合法值，表示"暂不灰度"）会让设备**永远起不来**。
// ===========================================================================

// 首启 + canary_percent = 0 → Start() 必须成功。
bool TestCanaryGateSkippedOnFirstStart() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto config = MakeValidConfig();
    config.enable_config_canary = true;
    config.config_canary_percent = 0U;
    config.config_canary_topic_prefix = "canary.";

    const auto start_ec = bridge.Start(config);
    const auto snapshot = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(start_ec == vr::core::ErrorCode::kOk,
                      "first start must succeed even when canary percent is 0") &&
           ExpectTrue(snapshot.bridge_metrics.reload_rollback_count == 0U,
                      "first start must not record a canary rollback");
}

// 已有一次成功提交之后，canary 门禁必须**仍然生效**（证明首启放行不是把门禁整体拆掉）。
//
// 说明：门禁位于 Start() 而非 ReloadConfigIfChanged()（reload 路径当前没有
// config-canary 门禁），所以这里用"启动 -> 停止 -> 用 canary_percent=0 再启动"
// 来触发同一段门禁代码。canary_percent = 0 时 gate 恒不命中，结果确定。
bool TestCanaryGateStillAppliesAfterCommit() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto config = MakeValidConfig();  // canary 默认关闭
    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "first start failed")) {
        return false;
    }
    bridge.Stop();

    auto gated = config;
    gated.enable_config_canary = true;
    gated.config_canary_percent = 0U;

    const auto second_ec = bridge.Start(gated);
    const auto snapshot = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(second_ec == vr::core::ErrorCode::kWouldBlock,
                      "second start must be gated by canary after a committed config") &&
           ExpectTrue(snapshot.bridge_metrics.reload_rollback_count >= 1U,
                      "canary rollback should be counted");
}

// 策略字段变更必须被 reload 拒绝，且原因**可区分**：这是产品限制
// （reload 只支持非策略字段），不是"配置损坏/签名不符"。两者排查路径完全不同，
// 所以要求 reason_code = 7 且审计摘要写 policy_fields_unsupported。
bool TestReloadRejectsPolicyFieldChange() {
    auto transport_tx = std::make_unique<NoopTransport>();
    auto transport_rx = std::make_unique<NoopTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(transport_tx), std::move(transport_rx));

    auto base_config = MakeValidConfig();  // lock_policy = false
    vr::interconnect::StaticConfigProvider provider(base_config, "static://policy_base");

    if (!ExpectTrue(bridge.Start(&provider) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    // 只改策略字段（延迟阈值），非策略字段一律不动。
    vr::interconnect::BridgeConfig changed = base_config;
    changed.policy_table.default_policy.max_end_to_end_latency_ms = 200;
    provider.UpdateConfigForTest(changed, "static://policy_changed");

    const auto reload_ec = bridge.ReloadConfigIfChanged(&provider);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    const auto audit = bridge.GetLastReloadAuditSummary();
    bridge.Stop();

    return ExpectTrue(reload_ec == vr::core::ErrorCode::kQueueCreateFailed,
                      "policy field change should be rejected") &&
           ExpectTrue(metrics.bridge_metrics.reload_rollback_reason_code == 7,
                      "reason code should be 7 (policy_fields_unsupported)") &&
           ExpectTrue(audit.find("policy_fields_unsupported") != std::string::npos,
                      "audit should name policy_fields_unsupported") &&
           ExpectTrue(audit.find("signature_mismatch") == std::string::npos,
                      "audit must not mislabel this as a signature mismatch");
}

}  // namespace

int main() {
    if (!TestReloadRollbackOnInvalidConfig()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestReloadRollbackOnPolicyLockViolation()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestCanarySegmentGateByTopicChannel()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestHighRiskConfigAutoRollback()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestCanaryGateSkippedOnFirstStart()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestCanaryGateStillAppliesAfterCommit()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    if (!TestReloadRejectsPolicyFieldChange()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect config reload test passed." << std::endl;
    return 0;
}
