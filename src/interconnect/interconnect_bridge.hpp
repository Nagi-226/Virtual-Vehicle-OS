#ifndef VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
#define VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"
#include "interconnect/bridge_policy.hpp"
#include "interconnect/config_provider.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/message_router.hpp"
#include "interconnect/system_metrics_aggregator.hpp"
#include "interconnect/transport.hpp"

namespace vr {
namespace interconnect {

struct BridgeConfig {
    TransportEndpointConfig vehicle_to_robot_endpoint;
    TransportEndpointConfig robot_to_vehicle_endpoint;
    vr::core::ThreadConfig thread_pool;
    BridgeSlaPolicy sla_policy;
    BridgePolicyTable policy_table;
    std::uint32_t receive_priority{0U};
};

class InterconnectBridge {
public:
    InterconnectBridge(std::unique_ptr<ITransport> vehicle_to_robot_transport,
                       std::unique_ptr<ITransport> robot_to_vehicle_transport);
    ~InterconnectBridge();

    vr::core::ErrorCode Start(const BridgeConfig& config) noexcept;
    vr::core::ErrorCode Start(IConfigProvider* provider) noexcept;
    void Stop() noexcept;

    vr::core::ErrorCode PublishFromVehicle(const MessageEnvelope& envelope) noexcept;
    vr::core::ErrorCode PublishFromRobot(const MessageEnvelope& envelope) noexcept;

    MessageRouter& VehicleRouter() noexcept;
    MessageRouter& RobotRouter() noexcept;

    BridgeMetrics GetBridgeMetrics() const noexcept;
    vr::core::ThreadPoolMetrics GetThreadPoolMetrics() const noexcept;
    MetricsSnapshot CaptureMetricsSnapshot() noexcept;
    MetricsDelta ExportMetricsDelta() noexcept;
    std::string GetLoadedConfigSource() const;
    std::uint64_t GetLoadedConfigVersion() const noexcept;
    vr::core::ErrorCode ReloadConfigIfChanged(IConfigProvider* provider) noexcept;

private:
    vr::core::ErrorCode Publish(ITransport* transport, const MessageEnvelope& envelope) noexcept;
    vr::core::ErrorCode PublishWithBackpressure(ITransport* transport, const std::string& encoded,
                                                std::uint32_t priority,
                                                const BridgeSlaPolicy& policy) noexcept;
    const BridgeSlaPolicy& ResolvePolicy(const MessageEnvelope& envelope) const noexcept;

    void VehicleInboundLoop() noexcept;
    void RobotInboundLoop() noexcept;
    void ProcessInbound(ITransport* transport, MessageRouter* router,
                        const std::string& loop_name) noexcept;

    void RefreshAggregatedMetrics(bool force = false) noexcept;
    bool ShouldRefreshMetrics(const std::uint64_t now_ms) noexcept;

    std::atomic<bool> running_{false};
    vr::core::ThreadPool worker_pool_;

    std::unique_ptr<ITransport> vehicle_to_robot_transport_;
    std::unique_ptr<ITransport> robot_to_vehicle_transport_;

    BridgeConfig config_{};
    MessageRouter vehicle_router_;
    MessageRouter robot_router_;

    std::atomic<std::uint64_t> tx_count_{0U};
    std::atomic<std::uint64_t> tx_fail_count_{0U};
    std::atomic<std::uint64_t> rx_count_{0U};
    std::atomic<std::uint64_t> encode_fail_count_{0U};
    std::atomic<std::uint64_t> decode_fail_count_{0U};
    std::atomic<std::uint64_t> expired_drop_count_{0U};
    std::atomic<std::uint64_t> route_miss_count_{0U};
    std::atomic<std::uint64_t> invalid_envelope_count_{0U};
    std::atomic<std::uint64_t> transport_error_count_{0U};
    std::atomic<std::uint64_t> backpressure_drop_count_{0U};

    std::atomic<std::uint64_t> last_metrics_refresh_ms_{0U};
    static constexpr std::uint64_t kMetricsRefreshIntervalMs = 100U;

    std::uint64_t loaded_config_version_{0U};
    std::string loaded_config_source_;
    mutable SystemMetricsAggregator metrics_aggregator_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
