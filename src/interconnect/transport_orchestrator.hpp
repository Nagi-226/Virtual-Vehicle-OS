#ifndef VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP
#define VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP

#include "interconnect/transport.hpp"

namespace vr {
namespace interconnect {

class TransportOrchestrator {
public:
    ITransport* ResolveFailover(ITransport* primary,
                                ITransport* vehicle_to_robot,
                                ITransport* robot_to_vehicle,
                                bool enable_v2r_failover,
                                bool enable_r2v_failover) const;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP
