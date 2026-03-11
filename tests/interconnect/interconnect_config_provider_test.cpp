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

bool ExpectTrue(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

bool TestConfigProviderStart() {
    vr::interconnect::BridgeConfig cfg;
    cfg.vehicle_to_robot_endpoint.name = "cfg_v2r";
    cfg.robot_to_vehicle_endpoint.name = "cfg_r2v";
    cfg.thread_pool.worker_count = 1;
    cfg.thread_pool.queue_capacity = 4;
    cfg.policy_table.default_policy = cfg.sla_policy;
    cfg.policy_table.default_policy = cfg.sla_policy;

    vr::interconnect::StaticConfigProvider provider(cfg, "static://unit-test");

    vr::interconnect::InterconnectBridge bridge(std::make_unique<NoopTransport>(),
                                                std::make_unique<NoopTransport>());

    const auto ec = bridge.Start(&provider);
    const auto source = bridge.GetLoadedConfigSource();
    const auto version = bridge.GetLoadedConfigVersion();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "Start via provider should succeed") &&
           ExpectTrue(source == "static://unit-test", "Loaded source should match provider") &&
           ExpectTrue(version == provider.GetVersion(), "Loaded version should match provider");
}

bool TestConfigReloadVersionChange() {
    vr::interconnect::BridgeConfig cfg;
    cfg.vehicle_to_robot_endpoint.name = "reload_v2r";
    cfg.robot_to_vehicle_endpoint.name = "reload_r2v";
    cfg.thread_pool.worker_count = 1;
    cfg.thread_pool.queue_capacity = 4;
    cfg.policy_table.default_policy = cfg.sla_policy;

    vr::interconnect::StaticConfigProvider provider(cfg, "static://reload");

    vr::interconnect::InterconnectBridge bridge(std::make_unique<NoopTransport>(),
                                                std::make_unique<NoopTransport>());

    const auto start_ec = bridge.Start(&provider);
    if (!ExpectTrue(start_ec == vr::core::ErrorCode::kOk, "Start should succeed")) {
        return false;
    }

    vr::interconnect::BridgeConfig next = cfg;
    next.receive_priority = 3U;
    provider.UpdateConfigForTest(next, "static://reload-v2");

    const auto reload_ec = bridge.ReloadConfigIfChanged(&provider);
    const auto version = bridge.GetLoadedConfigVersion();
    const auto source = bridge.GetLoadedConfigSource();
    bridge.Stop();

    return ExpectTrue(reload_ec == vr::core::ErrorCode::kOk, "Reload should succeed") &&
           ExpectTrue(version == provider.GetVersion(), "Version should update on reload") &&
           ExpectTrue(source == "static://reload-v2", "Source should update on reload");
}

}  // namespace

int main() {
    if (!TestConfigProviderStart()) {
        std::cerr << "interconnect config provider test failed." << std::endl;
        return 1;
    }
    if (!TestConfigReloadVersionChange()) {
        std::cerr << "interconnect config reload test failed." << std::endl;
        return 1;
    }
    std::cout << "interconnect config provider test passed." << std::endl;
    return 0;
}

