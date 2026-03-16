#include <chrono>
#include <cstdint>
#include <memory>
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

std::string MakeTraceId(const std::uint64_t seq) {
    return "trace_" + std::to_string(seq);
}

}  // namespace

int main() {
    commonsvc::Logger::Instance().SetMinLevel(commonsvc::LogLevel::kDebug);
    commonsvc::Logger::Instance().EnableConsole(true);
    commonsvc::Logger::Instance().SetDefaultContext("demo", "interconnect_learning");

    vr::interconnect::BridgeConfig config;
    config.vehicle_to_robot_endpoint.name = "/vr_vehicle_to_robot_demo";
    config.vehicle_to_robot_endpoint.max_messages = 32;
    config.vehicle_to_robot_endpoint.message_size = 1024;

    config.robot_to_vehicle_endpoint.name = "/vr_robot_to_vehicle_demo";
    config.robot_to_vehicle_endpoint.max_messages = 32;
    config.robot_to_vehicle_endpoint.message_size = 1024;

    config.thread_pool.worker_count = 2;
    config.thread_pool.queue_capacity = 16;
    config.thread_pool.rejection_policy = vr::core::RejectionPolicy::kRejectNewTask;

    config.sla_policy.max_end_to_end_latency_ms = 300;
    config.sla_policy.transport_receive_timeout_ms = 50;
    config.sla_policy.transport_send_timeout_ms = 50;
    config.sla_policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kDropOldest;
    config.sla_policy.drop_policy = vr::interconnect::DropPolicy::kDropOldest;
    config.sla_policy.retry_budget.max_retries = 2;
    config.sla_policy.retry_budget.initial_backoff_ms = 5;
    config.sla_policy.retry_budget.max_backoff_ms = 20;

    vr::interconnect::PolicyRule control_rule;
    control_rule.priority = 10U;
    control_rule.match_any_channel = false;
    control_rule.channel = vr::interconnect::ChannelType::kControl;
    control_rule.topic = "vehicle.command";
    control_rule.policy.transport_send_timeout_ms = 30;
    control_rule.policy.backpressure_policy = vr::interconnect::BackpressurePolicy::kDropOldest;
    control_rule.policy.drop_policy = vr::interconnect::DropPolicy::kDropOldest;
    control_rule.policy.retry_budget.max_retries = 1;
    control_rule.policy.retry_budget.initial_backoff_ms = 5;
    control_rule.policy.retry_budget.max_backoff_ms = 10;
    config.policy_table.rules.push_back(control_rule);

    vr::interconnect::PolicyRule conflict_rule = control_rule;
    conflict_rule.priority = control_rule.priority;
    conflict_rule.topic = "vehicle.command";
    conflict_rule.policy.transport_send_timeout_ms = 40;
    config.policy_table.rules.push_back(conflict_rule);

    vr::interconnect::InterconnectBridge bridge(std::make_unique<vr::interconnect::PosixMqTransport>(),
                                                std::make_unique<vr::interconnect::PosixMqTransport>());

    bridge.VehicleRouter().Register("robot.status", [](const vr::interconnect::MessageEnvelope& msg) {
        LOG_INFO("[Vehicle Side] recv from robot topic=" + msg.topic + ", payload=" + msg.payload);
    });

    bridge.RobotRouter().Register("vehicle.command", [](const vr::interconnect::MessageEnvelope& msg) {
        LOG_INFO("[Robot Side] recv from vehicle topic=" + msg.topic + ", payload=" + msg.payload);
    });

    const vr::core::ErrorCode start_ec = bridge.Start(config);
    if (start_ec != vr::core::ErrorCode::kOk) {
        LOG_ERROR_CODE(start_ec, "Bridge start failed");
        return 1;
    }

    for (std::uint64_t seq = 1; seq <= 5; ++seq) {
        vr::interconnect::MessageEnvelope vehicle_msg;
        vehicle_msg.channel = vr::interconnect::ChannelType::kControl;
        vehicle_msg.qos = vr::interconnect::DeliveryQoS::kAtLeastOnce;
        vehicle_msg.sequence = seq;
        vehicle_msg.timestamp_ms = NowMs();
        vehicle_msg.ttl_ms = 200;
        vehicle_msg.source = "vehicle.domain";
        vehicle_msg.target = "robot.domain";
        vehicle_msg.topic = "vehicle.command";
        vehicle_msg.trace_id = MakeTraceId(seq);
        vehicle_msg.payload = "move_forward_step_" + std::to_string(seq);
        (void)bridge.PublishFromVehicle(vehicle_msg);

        vr::interconnect::MessageEnvelope robot_msg;
        robot_msg.channel = vr::interconnect::ChannelType::kTelemetry;
        robot_msg.qos = vr::interconnect::DeliveryQoS::kBestEffort;
        robot_msg.sequence = seq;
        robot_msg.timestamp_ms = NowMs();
        robot_msg.ttl_ms = 200;
        robot_msg.source = "robot.domain";
        robot_msg.target = "vehicle.domain";
        robot_msg.topic = "robot.status";
        robot_msg.trace_id = MakeTraceId(1000 + seq);
        robot_msg.payload = "battery=" + std::to_string(90 - static_cast<int>(seq));
        (void)bridge.PublishFromRobot(robot_msg);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    const auto snapshot = bridge.CaptureMetricsSnapshot();
    const auto delta = bridge.ExportMetricsDelta();
    LOG_INFO("Bridge snapshot => tx=" + std::to_string(snapshot.bridge_metrics.tx_count) +
             ", rx=" + std::to_string(snapshot.bridge_metrics.rx_count) +
             ", decode_fail=" + std::to_string(snapshot.bridge_metrics.decode_fail_count) +
             ", expired=" + std::to_string(snapshot.bridge_metrics.expired_drop_count) +
             ", route_miss=" + std::to_string(snapshot.bridge_metrics.route_miss_count) +
             ", backpressure_drop=" +
             std::to_string(snapshot.bridge_metrics.backpressure_drop_count));

    LOG_INFO("Bridge delta => tx_delta=" + std::to_string(delta.bridge_metrics_delta.tx_count) +
             ", rx_delta=" + std::to_string(delta.bridge_metrics_delta.rx_count));

    LOG_INFO("Policy lint report:\n" + bridge.GetPolicyLintReport());
    const auto conflicts = bridge.DumpPolicyConflicts();
    if (conflicts.empty()) {
        LOG_INFO("Policy conflict check: clean");
    } else {
        for (const auto& conflict : conflicts) {
            LOG_WARN("Policy conflict => " + conflict);
        }
    }

    bridge.Stop();

    LOG_INFO("interconnect learning demo finished");
    return 0;
}
