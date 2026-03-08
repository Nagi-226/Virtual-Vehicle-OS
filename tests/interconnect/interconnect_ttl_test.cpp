#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/transport.hpp"

namespace {

class InMemoryTransport final : public vr::interconnect::ITransport {
public:
    vr::core::ErrorCode Create(const vr::interconnect::TransportEndpointConfig& config) noexcept override {
        (void)config;
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode SendWithTimeout(const std::string& message, const std::uint32_t priority,
                                        const std::int64_t timeout_ms) noexcept override {
        (void)priority;
        (void)timeout_ms;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(message);
        }
        cv_.notify_one();
        return vr::core::ErrorCode::kOk;
    }

    vr::core::ErrorCode ReceiveWithTimeout(std::string* const message, std::uint32_t* const priority,
                                           const std::int64_t timeout_ms) noexcept override {
        if (message == nullptr || priority == nullptr) {
            return vr::core::ErrorCode::kInvalidParam;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                                        [this]() { return !queue_.empty(); });
        if (!ready) {
            return vr::core::ErrorCode::kTimeout;
        }

        *message = queue_.front();
        queue_.pop();
        *priority = 0U;
        return vr::core::ErrorCode::kOk;
    }

    void Close() noexcept override {}
    void Unlink() noexcept override {}

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> queue_;
};

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestExpiredMessageDropped() {
    auto t1 = std::make_unique<InMemoryTransport>();
    auto t2 = std::make_unique<InMemoryTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(t1), std::move(t2));

    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "mem_v2r";
    config.robot_to_vehicle_endpoint.name = "mem_r2v";
    config.thread_pool.worker_count = 2;
    config.thread_pool.queue_capacity = 8;
    config.sla_policy.max_end_to_end_latency_ms = 5;
    config.sla_policy.transport_receive_timeout_ms = 5;
    config.sla_policy.transport_send_timeout_ms = 5;

    std::uint32_t routed_count = 0U;
    bridge.RobotRouter().Register("vehicle.command", [&routed_count](const vr::interconnect::MessageEnvelope&) {
        ++routed_count;
    });

    if (!ExpectTrue(bridge.Start(config) == vr::core::ErrorCode::kOk, "bridge start failed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "vehicle.command";
    msg.trace_id = "trace_expired";
    msg.payload = "cmd";

    const auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now())
                            .time_since_epoch()
                            .count();
    msg.timestamp_ms = static_cast<std::uint64_t>(now_ms - 50);
    msg.ttl_ms = 10;

    (void)bridge.PublishFromVehicle(msg);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto metrics = bridge.GetBridgeMetrics();
    bridge.Stop();

    return ExpectTrue(routed_count == 0U, "expired message should not be routed") &&
           ExpectTrue(metrics.expired_drop_count >= 1U, "expired_drop_count should increase");
}

}  // namespace

int main() {
    const bool ok = TestExpiredMessageDropped();
    if (!ok) {
        std::cerr << "interconnect ttl test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect ttl test passed." << std::endl;
    return 0;
}
