#ifndef VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
#define VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "core/thread_pool.hpp"
#include "interconnect/bridge_metrics.hpp"
#include "interconnect/bridge_policy.hpp"
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
    std::uint32_t receive_priority{0U};
};

class InterconnectBridge {
public:
    InterconnectBridge(std::unique_ptr<ITransport> vehicle_to_robot_transport,
                       std::unique_ptr<ITransport> robot_to_vehicle_transport);
    ~InterconnectBridge();

    vr::core::ErrorCode Start(const BridgeConfig& config) noexcept;
    void Stop() noexcept;

    vr::core::ErrorCode PublishFromVehicle(const MessageEnvelope& envelope) noexcept;
    vr::core::ErrorCode PublishFromRobot(const MessageEnvelope& envelope) noexcept;

    MessageRouter& VehicleRouter() noexcept;
    MessageRouter& RobotRouter() noexcept;

    BridgeMetrics GetBridgeMetrics() const noexcept;
    vr::core::ThreadPoolMetrics GetThreadPoolMetrics() const noexcept;

private:
    vr::core::ErrorCode Publish(ITransport* transport, const MessageEnvelope& envelope) noexcept;

    void VehicleInboundLoop() noexcept;
    void RobotInboundLoop() noexcept;
    void ProcessInbound(ITransport* transport, MessageRouter* router,
                        const std::string& loop_name) noexcept;

    void RefreshAggregatedMetrics() noexcept;

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
    std::atomic<std::uint64_t> decode_fail_count_{0U};
    std::atomic<std::uint64_t> expired_drop_count_{0U};
    std::atomic<std::uint64_t> route_miss_count_{0U};
    std::atomic<std::uint64_t> invalid_envelope_count_{0U};
    std::atomic<std::uint64_t> transport_error_count_{0U};

    mutable SystemMetricsAggregator metrics_aggregator_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_INTERCONNECT_BRIDGE_HPP
