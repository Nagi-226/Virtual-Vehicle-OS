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
        (void)message;
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
    void Close() noexcept override {}
    void Unlink() noexcept override {}
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

}  // namespace

int main() {
    if (!TestReloadRollbackOnInvalidConfig()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect config reload test passed." << std::endl;
    return 0;
}
