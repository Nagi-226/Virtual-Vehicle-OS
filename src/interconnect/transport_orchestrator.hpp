#ifndef VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP
#define VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP

#include <cstdint>

#include "interconnect/transport.hpp"

namespace vr {
namespace interconnect {

class TransportOrchestrator {
public:
    struct Policy {
        std::uint32_t primary_weight{100U};
        std::uint32_t secondary_weight{0U};
        std::uint32_t circuit_break_threshold{3U};
        std::uint32_t circuit_recover_threshold{2U};
    };

    ITransport* ResolveFailover(ITransport* primary,
                                ITransport* vehicle_to_robot,
                                ITransport* robot_to_vehicle,
                                bool enable_v2r_failover,
                                bool enable_r2v_failover) const;

    void SetPolicy(const Policy& policy) noexcept;

    void OnPrimarySendFailure() noexcept;
    void OnPrimarySendSuccess() noexcept;

    bool IsCircuitOpen() const noexcept;

private:
    Policy policy_{};
    std::uint32_t primary_fail_streak_{0U};
    std::uint32_t primary_success_streak_{0U};
    bool circuit_open_{false};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_TRANSPORT_ORCHESTRATOR_HPP
