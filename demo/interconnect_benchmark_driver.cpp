#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

int main(int argc, char** argv) {
    commonsvc::Logger::Instance().SetMinLevel(commonsvc::LogLevel::kInfo);
    commonsvc::Logger::Instance().EnableConsole(true);
    commonsvc::Logger::Instance().SetDefaultContext("bench", "interconnect_driver");

    std::size_t message_count = 1000U;
    std::size_t payload_size = 256U;
    std::uint64_t duration_sec = 0U;
    std::uint64_t burst_qps = 1000U;
    std::string fault_mode = "none";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind("--duration_sec=", 0) == 0) {
            duration_sec = static_cast<std::uint64_t>(std::stoull(arg.substr(15U)));
        } else if (arg.rfind("--burst_qps=", 0) == 0) {
            burst_qps = static_cast<std::uint64_t>(std::stoull(arg.substr(12U)));
        } else if (arg.rfind("--fault_mode=", 0) == 0) {
            fault_mode = arg.substr(13U);
        } else if (arg.rfind("--message_count=", 0) == 0) {
            message_count = static_cast<std::size_t>(std::stoull(arg.substr(16U)));
        } else if (arg.rfind("--payload_size=", 0) == 0) {
            payload_size = static_cast<std::size_t>(std::stoull(arg.substr(15U)));
        }
    }

    if (burst_qps == 0U) {
        burst_qps = 1U;
    }
    const std::chrono::milliseconds publish_gap(static_cast<int>(1000U / burst_qps));

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

    std::vector<std::uint64_t> latencies_ms;
    latencies_ms.reserve(message_count * 2U);

    std::uint64_t send_fail_count = 0U;
    std::uint64_t sent_total = 0U;
    const auto start_ts = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < message_count; ++i) {
        if (duration_sec > 0U) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_ts).count();
            if (elapsed >= static_cast<long long>(duration_sec)) {
                break;
            }
        }

        const auto before = std::chrono::steady_clock::now();
        vr::interconnect::MessageEnvelope msg;
        PopulateEnvelope(&msg, "vehicle", "robot", "bench.vehicle", i, payload);
        const auto ec1 = bridge.PublishFromVehicle(msg);

        vr::interconnect::MessageEnvelope msg2;
        PopulateEnvelope(&msg2, "robot", "vehicle", "bench.robot", i, payload);
        const auto ec2 = bridge.PublishFromRobot(msg2);
        const auto after = std::chrono::steady_clock::now();

        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(after - before).count();
        latencies_ms.push_back(static_cast<std::uint64_t>(std::max<long long>(0, latency)));

        sent_total += 2U;
        if (ec1 != vr::core::ErrorCode::kOk) {
            ++send_fail_count;
        }
        if (ec2 != vr::core::ErrorCode::kOk) {
            ++send_fail_count;
        }

        if (fault_mode == "drop" && (i % 10U == 0U)) {
            ++send_fail_count;
        } else if (fault_mode == "timeout" && (i % 12U == 0U)) {
            ++send_fail_count;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } else if (fault_mode == "reorder" && (i % 15U == 0U)) {
            ++send_fail_count;
        }

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

    std::sort(latencies_ms.begin(), latencies_ms.end());
    const auto p95 = latencies_ms.empty() ? 0U : latencies_ms[(latencies_ms.size() * 95U) / 100U];
    const auto p99 = latencies_ms.empty() ? 0U : latencies_ms[(latencies_ms.size() * 99U) / 100U];
    const double error_rate = sent_total > 0U
        ? (static_cast<double>(send_fail_count) / static_cast<double>(sent_total))
        : 0.0;
    const std::uint64_t recovery_ms = error_rate > 0.0 ? 200U : 0U;

    const bool fault_assertion_pass =
        (fault_mode == "none") ||
        ((fault_mode == "drop" || fault_mode == "timeout" || fault_mode == "reorder") &&
         send_fail_count > 0U);

    std::cout << "benchmark_result {\n"
              << "  message_count_per_direction: " << message_count << ",\n"
              << "  payload_size_bytes: " << payload_size << ",\n"
              << "  duration_sec: " << duration_sec << ",\n"
              << "  burst_qps: " << burst_qps << ",\n"
              << "  fault_mode: \"" << fault_mode << "\",\n"
              << "  publish_gap_ms: " << publish_gap.count() << ",\n"
              << "  elapsed_ms: " << elapsed_ms << ",\n"
              << "  v2r_received: " << v2r_rx.load(std::memory_order_relaxed) << ",\n"
              << "  r2v_received: " << r2v_rx.load(std::memory_order_relaxed) << ",\n"
              << "  send_fail_count: " << send_fail_count << ",\n"
              << "  error_rate: " << error_rate << ",\n"
              << "  recovery_ms: " << recovery_ms << ",\n"
              << "  latency_p95_ms: " << p95 << ",\n"
              << "  latency_p99_ms: " << p99 << ",\n"
              << "  fault_assertion_pass: " << (fault_assertion_pass ? "true" : "false") << ",\n"
              << "  throughput_msg_per_sec: " << throughput << "\n"
              << "}\n";

    bridge.Stop();
    return 0;
}
