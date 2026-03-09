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

    vr::interconnect::StaticConfigProvider provider(cfg, "static://unit-test");

    vr::interconnect::InterconnectBridge bridge(std::make_unique<NoopTransport>(),
                                                std::make_unique<NoopTransport>());

    const auto ec = bridge.Start(&provider);
    const auto source = bridge.GetLoadedConfigSource();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "Start via provider should succeed") &&
           ExpectTrue(source == "static://unit-test", "Loaded source should match provider");
}

}  // namespace

int main() {
    if (!TestConfigProviderStart()) {
        std::cerr << "interconnect config provider test failed." << std::endl;
        return 1;
    }
    std::cout << "interconnect config provider test passed." << std::endl;
    return 0;
}

