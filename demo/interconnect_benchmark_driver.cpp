#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/posix_mq_transport.hpp"
#include "log/logger.hpp"

namespace {

std::uint64_t NowMs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

std::string MakePayload(const std::size_t size) {
    return std::string(size, 'x');
}

void PopulateEnvelope(vr::interconnect::MessageEnvelope* envelope,
                      const std::string& source,
                      const std::string& target,
                      const std::string& topic,
                      const std::uint64_t seq,
                      const std::string& payload) {
    envelope->source = source;
    envelope->target = target;
    envelope->topic = topic;
    envelope->channel = vr::interconnect::ChannelType::kTelemetry;
    envelope->qos = vr::interconnect::DeliveryQoS::kBestEffort;
    envelope->sequence = seq;
    envelope->timestamp_ms = NowMs();
    envelope->ttl_ms = 2000U;
    envelope->trace_id = "bench_" + std::to_string(seq);
    envelope->payload = payload;
}

}  // namespace

int main() {
    commonsvc::Logger::Instance().SetMinLevel(commonsvc::LogLevel::kInfo);
    commonsvc::Logger::Instance().EnableConsole(true);
    commonsvc::Logger::Instance().SetDefaultContext("bench", "interconnect_driver");

    const std::size_t message_count = 1000U;
    const std::size_t payload_size = 256U;
    const std::chrono::milliseconds publish_gap(1);

    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "/vr_bench_v2r";
    config.vehicle_to_robot_endpoint.max_messages = 64;
    config.vehicle_to_robot_endpoint.message_size = 2048;

    config.robot_to_vehicle_endpoint.name = "/vr_bench_r2v";
    config.robot_to_vehicle_endpoint.max_messages = 64;
    config.robot_to_vehicle_endpoint.message_size = 2048;

    config.thread_pool.worker_count = 2;
    config.thread_pool.queue_capacity = 64;

    config.sla_policy.max_end_to_end_latency_ms = 200;
    config.sla_policy.transport_receive_timeout_ms = 20;
    config.sla_policy.transport_send_timeout_ms = 20;
    config.sla_policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kReject;
    config.policy_table.default_policy = config.sla_policy;

    auto tx = std::make_unique<vr::interconnect::PosixMqTransport>();
    auto rx = std::make_unique<vr::interconnect::PosixMqTransport>();
    vr::interconnect::InterconnectBridge bridge(std::move(tx), std::move(rx));

    std::atomic<std::uint64_t> v2r_rx{0U};
    std::atomic<std::uint64_t> r2v_rx{0U};

    bridge.RobotRouter().Register("bench.vehicle", [&v2r_rx](const vr::interconnect::MessageEnvelope&) {
        v2r_rx.fetch_add(1U, std::memory_order_relaxed);
    });
    bridge.VehicleRouter().Register("bench.robot", [&r2v_rx](const vr::interconnect::MessageEnvelope&) {
        r2v_rx.fetch_add(1U, std::memory_order_relaxed);
    });

    const auto start_ec = bridge.Start(config);
    if (start_ec != vr::core::ErrorCode::kOk) {
        std::cerr << "Benchmark start failed" << std::endl;
        return 1;
    }

    const std::string payload = MakePayload(payload_size);

    const auto start_ts = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < message_count; ++i) {
        vr::interconnect::MessageEnvelope msg;
        PopulateEnvelope(&msg, "vehicle", "robot", "bench.vehicle", i, payload);
        (void)bridge.PublishFromVehicle(msg);

        vr::interconnect::MessageEnvelope msg2;
        PopulateEnvelope(&msg2, "robot", "vehicle", "bench.robot", i, payload);
        (void)bridge.PublishFromRobot(msg2);

        std::this_thread::sleep_for(publish_gap);
    }

    const auto expected = message_count;
    const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < wait_deadline) {
        if (v2r_rx.load(std::memory_order_relaxed) >= expected &&
            r2v_rx.load(std::memory_order_relaxed) >= expected) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const auto end_ts = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_ts - start_ts).count();
    const std::uint64_t total_messages = v2r_rx.load(std::memory_order_relaxed) +
        r2v_rx.load(std::memory_order_relaxed);
    const double throughput = elapsed_ms > 0
        ? static_cast<double>(total_messages) * 1000.0 / static_cast<double>(elapsed_ms)
        : 0.0;

    std::cout << "benchmark_result {\n"
              << "  message_count_per_direction: " << message_count << ",\n"
              << "  payload_size_bytes: " << payload_size << ",\n"
              << "  publish_gap_ms: " << publish_gap.count() << ",\n"
              << "  elapsed_ms: " << elapsed_ms << ",\n"
              << "  v2r_received: " << v2r_rx.load(std::memory_order_relaxed) << ",\n"
              << "  r2v_received: " << r2v_rx.load(std::memory_order_relaxed) << ",\n"
              << "  throughput_msg_per_sec: " << throughput << "\n"
              << "}\n";

    bridge.Stop();
    return 0;
}
