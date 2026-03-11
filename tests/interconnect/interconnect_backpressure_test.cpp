#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/transport.hpp"

namespace {

class BackpressureTransport final : public vr::interconnect::ITransport {
public:
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
        if (send_calls_ == 1U) {
            return vr::core::ErrorCode::kWouldBlock;
        }
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           std::int64_t timeout_ms) noexcept override {
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

bool TestDropOldestBackpressure() {
    auto tx = std::make_unique<BackpressureTransport>();
    auto rx = std::make_unique<BackpressureTransport>();

    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));

    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "bp_v2r";
    config.robot_to_vehicle_endpoint.name = "bp_r2v";
    config.thread_pool.worker_count = 1;
    config.thread_pool.queue_capacity = 4;
    config.sla_policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kDropOldest;
    config.policy_table.default_policy = config.sla_policy;
    config.policy_table.default_policy.backpressure_policy =
        vr::interconnect::BackpressurePolicy::kDropOldest;

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "vehicle.command";
    msg.trace_id = "bp";
    msg.payload = "cmd";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    const auto metrics = bridge.GetBridgeMetrics();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "publish should recover after drop-oldest") &&
           ExpectTrue(tx_raw->discard_calls() >= 1U, "discard oldest should be called") &&
           ExpectTrue(tx_raw->send_calls() >= 2U, "send should retry") &&
           ExpectTrue(metrics.backpressure_drop_count >= 1U, "backpressure metric should increase");
}

}  // namespace

int main() {
    const bool ok = TestDropOldestBackpressure();
    if (!ok) {
        std::cerr << "interconnect backpressure test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect backpressure test passed." << std::endl;
    return 0;
}
