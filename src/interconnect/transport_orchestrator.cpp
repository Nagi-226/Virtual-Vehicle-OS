#include "interconnect/transport_orchestrator.hpp"

namespace vr {
namespace interconnect {

ITransport* TransportOrchestrator::ResolveFailover(ITransport* primary,
                                                   ITransport* vehicle_to_robot,
                                                   ITransport* robot_to_vehicle,
                                                   const bool enable_v2r_failover,
                                                   const bool enable_r2v_failover) const {
    if (primary == vehicle_to_robot && enable_v2r_failover) {
        return robot_to_vehicle;
    }
    if (primary == robot_to_vehicle && enable_r2v_failover) {
        return vehicle_to_robot;
    }
    return nullptr;
}

}  // namespace interconnect
}  // namespace vr
