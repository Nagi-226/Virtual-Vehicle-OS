#include "interconnect/transport_orchestrator.hpp"

namespace vr {
namespace interconnect {

ITransport* TransportOrchestrator::ResolveFailover(ITransport* primary,
                                                   ITransport* vehicle_to_robot,
                                                   ITransport* robot_to_vehicle,
                                                   const bool enable_v2r_failover,
                                                   const bool enable_r2v_failover) const {
    if (circuit_open_) {
        if (primary == vehicle_to_robot && enable_v2r_failover) {
            return robot_to_vehicle;
        }
        if (primary == robot_to_vehicle && enable_r2v_failover) {
            return vehicle_to_robot;
        }
    }

    if (policy_.secondary_weight > 0U) {
        if (primary == vehicle_to_robot && enable_v2r_failover) {
            return robot_to_vehicle;
        }
        if (primary == robot_to_vehicle && enable_r2v_failover) {
            return vehicle_to_robot;
        }
    }

    if (primary == vehicle_to_robot && enable_v2r_failover) {
        return robot_to_vehicle;
    }
    if (primary == robot_to_vehicle && enable_r2v_failover) {
        return vehicle_to_robot;
    }
    return nullptr;
}

void TransportOrchestrator::SetPolicy(const Policy& policy) noexcept {
    policy_ = policy;
}

void TransportOrchestrator::OnPrimarySendFailure() noexcept {
    ++primary_fail_streak_;
    primary_success_streak_ = 0U;

    if (primary_fail_streak_ >= policy_.circuit_break_threshold) {
        circuit_open_ = true;
    }
}

void TransportOrchestrator::OnPrimarySendSuccess() noexcept {
    primary_fail_streak_ = 0U;
    ++primary_success_streak_;

    if (circuit_open_ && primary_success_streak_ >= policy_.circuit_recover_threshold) {
        circuit_open_ = false;
        primary_success_streak_ = 0U;
    }
}

bool TransportOrchestrator::IsCircuitOpen() const noexcept {
    return circuit_open_;
}

}  // namespace interconnect
}  // namespace vr
