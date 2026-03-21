#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/transport.hpp"

namespace {

class FaultInjectTransport final : public vr::interconnect::ITransport {
public:
    enum class SendFault {
        kNone,
        kTimeoutOnce,
        kWouldBlockOnce
    };

    explicit FaultInjectTransport(SendFault fault) : fault_(fault) {}

    vr::core::ErrorCode Create(const vr::interconnect::TransportEndpointConfig& config) noexcept override {
        (void)config;
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                        std::int64_t timeout_ms) noexcept override {
        (void)message;
        (void)priority;
        (void)timeout_ms;
        ++send_calls_;
        if (fault_ == SendFault::kTimeoutOnce && send_calls_ == 1U) {
            return vr::core::ErrorCode::kTimeout;
        }
        if (fault_ == SendFault::kWouldBlockOnce && send_calls_ == 1U) {
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
    SendFault fault_{SendFault::kNone};
    std::uint32_t send_calls_{0U};
    std::uint32_t discard_calls_{0U};
};

bool ExpectTrue(const bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

vr::interconnect::BridgeConfig MakeRetryConfig() {
    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "fault_v2r";
    config.robot_to_vehicle_endpoint.name = "fault_r2v";
    config.thread_pool.worker_count = 1;
    config.thread_pool.queue_capacity = 4;
    config.sla_policy.retry_budget.max_retries = 1;
    config.sla_policy.retry_budget.initial_backoff_ms = 5;
    config.sla_policy.retry_budget.max_backoff_ms = 5;
    config.policy_table.default_policy = config.sla_policy;
    return config;
}

bool TestRetryOnWouldBlock() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kWouldBlockOnce);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.retry";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "retry should succeed") &&
           ExpectTrue(tx_raw->send_calls() >= 2U, "retry should attempt twice");
}

bool TestTimeoutDrop() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kTimeoutOnce);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto* tx_raw = tx.get();

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();
    config.sla_policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kDropOldest;
    config.sla_policy.drop_policy = vr::interconnect::DropPolicy::kDropOldest;
    config.policy_table.default_policy = config.sla_policy;

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.timeout";
    msg.channel = vr::interconnect::ChannelType::kControl;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;

    const auto ec = bridge.PublishFromVehicle(msg);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kOk, "timeout retry should succeed") &&
           ExpectTrue(tx_raw->send_calls() >= 2U, "timeout retry should attempt twice") &&
           ExpectTrue(metrics.bridge_metrics.backpressure_drop_count >= 1U,
                      "backpressure drop should be recorded") &&
           ExpectTrue(tx_raw->discard_calls() >= 1U,
                      "discard oldest should be invoked");
}

bool TestHandlerException() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    bridge.VehicleRouter().Register("fault.handler", [](const vr::interconnect::MessageEnvelope&) {
        throw std::runtime_error("handler failure");
    });

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.handler";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;

    (void)bridge.PublishFromRobot(msg);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(metrics.bridge_metrics.handler_error_count >= 1U,
                      "handler error should be counted");
}

bool TestRouteMiss() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.miss";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;

    (void)bridge.PublishFromVehicle(msg);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(metrics.bridge_metrics.route_miss_count >= 1U,
                      "route miss should be counted");
}

bool TestExpiredDrop() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.expired";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;
    msg.ttl_ms = 1U;

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto ec = bridge.PublishFromVehicle(msg);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(ec == vr::core::ErrorCode::kTimeout ||
                      ec == vr::core::ErrorCode::kExpired,
                      "expired should drop") &&
           ExpectTrue(metrics.bridge_metrics.expired_drop_count >= 1U,
                      "expired drop should be counted");
}

bool TestOutOfOrderAndDuplicate() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg_a;
    msg_a.source = "vehicle";
    msg_a.target = "robot";
    msg_a.topic = "fault.order";
    msg_a.channel = vr::interconnect::ChannelType::kEvent;
    msg_a.payload = "payload_a";
    msg_a.sequence = 2U;
    msg_a.timestamp_ms = 1U;

    vr::interconnect::MessageEnvelope msg_b = msg_a;
    msg_b.payload = "payload_b";
    msg_b.sequence = 1U;

    const auto ec1 = bridge.PublishFromVehicle(msg_a);
    const auto ec2 = bridge.PublishFromVehicle(msg_b);
    const auto ec3 = bridge.PublishFromVehicle(msg_b);
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(ec1 == vr::core::ErrorCode::kOk, "out-of-order should succeed") &&
           ExpectTrue(ec2 == vr::core::ErrorCode::kOk, "out-of-order should succeed") &&
           ExpectTrue(ec3 == vr::core::ErrorCode::kOk, "duplicate should succeed") &&
           ExpectTrue(metrics.bridge_metrics.tx_count >= 3U, "tx count should record duplicates");
}

bool TestFailoverHealthAndDiagCounters() {
    auto primary = std::make_unique<FaultInjectTransport>(
        FaultInjectTransport::SendFault::kWouldBlockOnce);
    auto secondary = std::make_unique<FaultInjectTransport>(
        FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(primary), std::move(secondary));
    auto config = MakeRetryConfig();

    vr::interconnect::TransportEndpointConfig extra;
    extra.name = "backup_transport";
    config.additional_endpoints.push_back(extra);

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "fault.failover";
    msg.channel = vr::interconnect::ChannelType::kEvent;
    msg.payload = "payload";
    msg.timestamp_ms = 1U;

    const auto first_ec = bridge.PublishFromVehicle(msg);
    const auto metrics_after_failover = bridge.CaptureMetricsSnapshot();

    const auto second_ec = bridge.PublishFromVehicle(msg);
    const auto metrics_after_recovery = bridge.CaptureMetricsSnapshot();

    (void)bridge.DumpRuntimeState();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const auto metrics = bridge.CaptureMetricsSnapshot();
    bridge.Stop();

    return ExpectTrue(first_ec == vr::core::ErrorCode::kOk, "failover publish should succeed") &&
           ExpectTrue(metrics_after_failover.bridge_metrics.failover_hit_count >= 1U,
                      "failover hit count should increase") &&
           ExpectTrue(metrics_after_failover.bridge_metrics.transport_primary_healthy == 0U,
                      "primary should be unhealthy right after failover") &&
           ExpectTrue(second_ec == vr::core::ErrorCode::kOk, "primary recovery publish should succeed") &&
           ExpectTrue(metrics_after_recovery.bridge_metrics.transport_primary_healthy >= 1U,
                      "primary should recover healthy on next success") &&
           ExpectTrue(metrics.bridge_metrics.transport_secondary_healthy >= 1U,
                      "secondary should be healthy") &&
           ExpectTrue(metrics.bridge_metrics.diag_dump_state_count >= 1U,
                      "dump diagnostic count should increase");
}

bool TestDiagnosticCommandInterface() {
    auto tx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);
    auto rx = std::make_unique<FaultInjectTransport>(FaultInjectTransport::SendFault::kNone);

    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));
    auto config = MakeRetryConfig();
    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    const std::string runtime_dump = bridge.ExecuteDiagnosticCommand("dump runtime");
    const std::string policy_dump = bridge.ExecuteDiagnosticCommand("dump policy");
    const std::string transport_dump = bridge.ExecuteDiagnosticCommand("dump transport");
    const std::string cache_dump = bridge.ExecuteDiagnosticCommand("dump cache");
    const std::string unknown = bridge.ExecuteDiagnosticCommand("dump unknown");
    const auto metrics = bridge.CaptureMetricsSnapshot();

    bridge.Stop();

    return ExpectTrue(runtime_dump.find("loaded_config_version") != std::string::npos,
                      "runtime dump should contain config version") &&
           ExpectTrue(!policy_dump.empty(), "policy dump should not be empty") &&
           ExpectTrue(transport_dump.find("transport") != std::string::npos,
                      "transport dump should be available") &&
           ExpectTrue(cache_dump.find("policy_cache_size") != std::string::npos,
                      "cache dump should contain cache size") &&
           ExpectTrue(unknown.find("unknown_command") != std::string::npos,
                      "unknown command should return diagnostic error") &&
           ExpectTrue(metrics.bridge_metrics.diag_dump_state_count >= 1U,
                      "diag dump metric should increase");
}

}  // namespace

int main() {
    if (!TestRetryOnWouldBlock()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestTimeoutDrop()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestHandlerException()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestRouteMiss()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestExpiredDrop()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestOutOfOrderAndDuplicate()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestFailoverHealthAndDiagCounters()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }
    if (!TestDiagnosticCommandInterface()) {
        std::cerr << "interconnect fault injection test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect fault injection test passed." << std::endl;
    return 0;
}
