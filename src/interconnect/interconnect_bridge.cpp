#include "interconnect/interconnect_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

#include "interconnect/message_codec.hpp"
#include "log/logger.hpp"

namespace vr {
namespace interconnect {

namespace {

std::uint64_t NowUnixMs() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

//消息过期判断
bool IsExpired(const MessageEnvelope& envelope, const std::uint64_t now_ms,
               const std::uint32_t max_latency_ms) {
    const std::uint32_t effective_ttl =
        envelope.ttl_ms == 0U ? max_latency_ms : std::min(envelope.ttl_ms, max_latency_ms);

    if (effective_ttl == 0U) {
        return false;
    }
    if (now_ms < envelope.timestamp_ms) {
        return false;
    }
    return (now_ms - envelope.timestamp_ms) > effective_ttl;
}

bool IsEnvelopeValidForPublish(const MessageEnvelope& envelope) {
    return !envelope.source.empty() && !envelope.target.empty() && !envelope.topic.empty();
}

}  // namespace

InterconnectBridge::InterconnectBridge(std::unique_ptr<ITransport> vehicle_to_robot_transport,
                                       std::unique_ptr<ITransport> robot_to_vehicle_transport)
    : vehicle_to_robot_transport_(std::move(vehicle_to_robot_transport)),
      robot_to_vehicle_transport_(std::move(robot_to_vehicle_transport)) {}

InterconnectBridge::~InterconnectBridge() {
    Stop();
}

vr::core::ErrorCode InterconnectBridge::Start(IConfigProvider* const provider) noexcept {
    if (provider == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    BridgeConfig cfg;
    std::string source;
    const vr::core::ErrorCode ec = provider->LoadBridgeConfig(&cfg, &source);
    if (ec != vr::core::ErrorCode::kOk) {
        return ec;
    }

    loaded_config_source_ = source;
    return Start(cfg);
}

vr::core::ErrorCode InterconnectBridge::Start(const BridgeConfig& config) noexcept {
    if (running_.load(std::memory_order_acquire)) {
        return vr::core::ErrorCode::kOk;
    }

    if (loaded_config_source_.empty()) {
        loaded_config_source_ = "direct";
    }

    if (!vehicle_to_robot_transport_ || !robot_to_vehicle_transport_) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    config_ = config;

    vr::core::ErrorCode ec = vehicle_to_robot_transport_->Create(config_.vehicle_to_robot_endpoint);
    if (ec != vr::core::ErrorCode::kOk) {
        return ec;
    }

    ec = robot_to_vehicle_transport_->Create(config_.robot_to_vehicle_endpoint);
    if (ec != vr::core::ErrorCode::kOk) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
        return ec;
    }

    ec = worker_pool_.Start(config_.thread_pool);
    if (ec != vr::core::ErrorCode::kOk) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
        robot_to_vehicle_transport_->Close();
        robot_to_vehicle_transport_->Unlink();
        return ec;
    }

    running_.store(true, std::memory_order_release);

    ec = worker_pool_.Enqueue([this]() { VehicleInboundLoop(); });
    if (ec != vr::core::ErrorCode::kOk && ec != vr::core::ErrorCode::kThreadTaskRejected) {
        Stop();
        return ec;
    }

    ec = worker_pool_.Enqueue([this]() { RobotInboundLoop(); });
    if (ec != vr::core::ErrorCode::kOk && ec != vr::core::ErrorCode::kThreadTaskRejected) {
        Stop();
        return ec;
    }

    RefreshAggregatedMetrics();
    return vr::core::ErrorCode::kOk;
}

void InterconnectBridge::Stop() noexcept {
    if (!running_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    worker_pool_.Stop();

    if (vehicle_to_robot_transport_) {
        vehicle_to_robot_transport_->Close();
        vehicle_to_robot_transport_->Unlink();
    }

    if (robot_to_vehicle_transport_) {
        robot_to_vehicle_transport_->Close();
        robot_to_vehicle_transport_->Unlink();
    }

    RefreshAggregatedMetrics(true);
}

vr::core::ErrorCode InterconnectBridge::PublishFromVehicle(const MessageEnvelope& envelope) noexcept {
    return Publish(vehicle_to_robot_transport_.get(), envelope);
}

vr::core::ErrorCode InterconnectBridge::PublishFromRobot(const MessageEnvelope& envelope) noexcept {
    return Publish(robot_to_vehicle_transport_.get(), envelope);
}

MessageRouter& InterconnectBridge::VehicleRouter() noexcept {
    return vehicle_router_;
}

MessageRouter& InterconnectBridge::RobotRouter() noexcept {
    return robot_router_;
}

BridgeMetrics InterconnectBridge::GetBridgeMetrics() const noexcept {
    return metrics_aggregator_.GetBridgeMetrics();
}

vr::core::ThreadPoolMetrics InterconnectBridge::GetThreadPoolMetrics() const noexcept {
    return metrics_aggregator_.GetThreadPoolMetrics();
}

MetricsSnapshot InterconnectBridge::CaptureMetricsSnapshot() noexcept {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.CaptureSnapshot();
}

MetricsDelta InterconnectBridge::ExportMetricsDelta() noexcept {
    RefreshAggregatedMetrics(true);
    return metrics_aggregator_.ExportDeltaSinceLastCall();
}

std::string InterconnectBridge::GetLoadedConfigSource() const {
    return loaded_config_source_;
}

vr::core::ErrorCode InterconnectBridge::PublishWithBackpressure(ITransport* const transport,
                                                                const std::string& encoded,
                                                                const std::uint32_t priority) noexcept {
    vr::core::ErrorCode send_ec =
        transport->SendWithTimeout(encoded, priority, config_.sla_policy.transport_send_timeout_ms);
    if (send_ec == vr::core::ErrorCode::kOk) {
        return send_ec;
    }

    if (config_.sla_policy.backpressure_policy == BackpressurePolicy::kDropOldest &&
        (send_ec == vr::core::ErrorCode::kTimeout || send_ec == vr::core::ErrorCode::kThreadQueueFull ||
         send_ec == vr::core::ErrorCode::kWouldBlock)) {
        const vr::core::ErrorCode discard_ec = transport->DiscardOldest();
        if (discard_ec == vr::core::ErrorCode::kOk) {
            backpressure_drop_count_.fetch_add(1U, std::memory_order_relaxed);
            send_ec = transport->SendWithTimeout(encoded, priority,
                                                 config_.sla_policy.transport_send_timeout_ms);
        }
    }

    return send_ec;
}

vr::core::ErrorCode InterconnectBridge::Publish(ITransport* const transport,
                                                const MessageEnvelope& envelope) noexcept {
    if (!running_.load(std::memory_order_acquire) || transport == nullptr) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kThreadStartFailed;
    }

    if (!IsEnvelopeValidForPublish(envelope)) {
        invalid_envelope_count_.fetch_add(1U, std::memory_order_relaxed);
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return vr::core::ErrorCode::kInterconnectInvalidEnvelope;
    }

    std::string encoded;
    const vr::core::ErrorCode encode_ec = MessageCodec::Encode(envelope, &encoded);
    if (encode_ec != vr::core::ErrorCode::kOk) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        encode_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return encode_ec;
    }

    const vr::core::ErrorCode send_ec =
        PublishWithBackpressure(transport, encoded, config_.receive_priority);

    if (send_ec != vr::core::ErrorCode::kOk) {
        tx_fail_count_.fetch_add(1U, std::memory_order_relaxed);
        transport_error_count_.fetch_add(1U, std::memory_order_relaxed);
        RefreshAggregatedMetrics();
        return send_ec;
    }

    tx_count_.fetch_add(1U, std::memory_order_relaxed);
    RefreshAggregatedMetrics();
    return vr::core::ErrorCode::kOk;
}

void InterconnectBridge::VehicleInboundLoop() noexcept {
    ProcessInbound(vehicle_to_robot_transport_.get(), &robot_router_, "vehicle_to_robot");
}

void InterconnectBridge::RobotInboundLoop() noexcept {
    ProcessInbound(robot_to_vehicle_transport_.get(), &vehicle_router_, "robot_to_vehicle");
}

void InterconnectBridge::ProcessInbound(ITransport* const transport, MessageRouter* const router,
                                        const std::string& loop_name) noexcept {
    while (running_.load(std::memory_order_acquire)) {
        std::string text;
        std::uint32_t priority = 0U;

        const vr::core::ErrorCode recv_ec =
            transport->ReceiveWithTimeout(&text, &priority,
                                          config_.sla_policy.transport_receive_timeout_ms);
        if (recv_ec == vr::core::ErrorCode::kTimeout || recv_ec == vr::core::ErrorCode::kWouldBlock) {
            continue;
        }

        if (recv_ec != vr::core::ErrorCode::kOk) {
            transport_error_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge receive failed in " + loop_name);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            RefreshAggregatedMetrics();
            continue;
        }

        rx_count_.fetch_add(1U, std::memory_order_relaxed);

        MessageEnvelope envelope;
        const vr::core::ErrorCode decode_ec = MessageCodec::Decode(text, &envelope);
        if (decode_ec != vr::core::ErrorCode::kOk) {
            decode_fail_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge decode failed in " + loop_name);
            RefreshAggregatedMetrics();
            continue;
        }

        if (!IsEnvelopeValidForPublish(envelope)) {
            invalid_envelope_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge invalid envelope in " + loop_name);
            RefreshAggregatedMetrics();
            continue;
        }

        const std::uint64_t now_ms = NowUnixMs();
        if (IsExpired(envelope, now_ms, config_.sla_policy.max_end_to_end_latency_ms)) {
            expired_drop_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge dropped expired message topic: " + envelope.topic);
            RefreshAggregatedMetrics();
            continue;
        }

        if (!router->Route(envelope)) {
            route_miss_count_.fetch_add(1U, std::memory_order_relaxed);
            LOG_WARN("Bridge route missed topic: " + envelope.topic);
            RefreshAggregatedMetrics();
            continue;
        }

        RefreshAggregatedMetrics();
    }
}

bool InterconnectBridge::ShouldRefreshMetrics(const std::uint64_t now_ms) noexcept {
    const std::uint64_t last = last_metrics_refresh_ms_.load(std::memory_order_relaxed);
    if (now_ms < last) {
        return true;
    }
    return (now_ms - last) >= kMetricsRefreshIntervalMs;
}

void InterconnectBridge::RefreshAggregatedMetrics(const bool force) noexcept {
    const std::uint64_t now_ms = NowUnixMs();
    if (!force && !ShouldRefreshMetrics(now_ms)) {
        return;
    }

    last_metrics_refresh_ms_.store(now_ms, std::memory_order_relaxed);

    BridgeMetrics bridge_metrics;
    bridge_metrics.tx_count = tx_count_.load(std::memory_order_relaxed);
    bridge_metrics.tx_fail_count = tx_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.rx_count = rx_count_.load(std::memory_order_relaxed);
    bridge_metrics.encode_fail_count = encode_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.decode_fail_count = decode_fail_count_.load(std::memory_order_relaxed);
    bridge_metrics.expired_drop_count = expired_drop_count_.load(std::memory_order_relaxed);
    bridge_metrics.route_miss_count = route_miss_count_.load(std::memory_order_relaxed);
    bridge_metrics.invalid_envelope_count = invalid_envelope_count_.load(std::memory_order_relaxed);
    bridge_metrics.transport_error_count = transport_error_count_.load(std::memory_order_relaxed);
    bridge_metrics.backpressure_drop_count = backpressure_drop_count_.load(std::memory_order_relaxed);

    metrics_aggregator_.UpdateBridgeMetrics(bridge_metrics);
    metrics_aggregator_.UpdateThreadPoolMetrics(worker_pool_.GetMetrics());
}

}  // namespace interconnect
}  // namespace vr

