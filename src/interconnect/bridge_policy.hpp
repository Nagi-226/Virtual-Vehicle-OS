#ifndef VR_INTERCONNECT_BRIDGE_POLICY_HPP
#define VR_INTERCONNECT_BRIDGE_POLICY_HPP

#include <cstdint>

namespace vr {
namespace interconnect {

enum class BackpressurePolicy : std::uint8_t {
    kReject = 0,
    kDropOldest = 1
};

struct BridgeSlaPolicy {
    std::uint32_t max_end_to_end_latency_ms{100};
    std::int64_t transport_receive_timeout_ms{50};
    std::int64_t transport_send_timeout_ms{50};
    BackpressurePolicy backpressure_policy{BackpressurePolicy::kReject};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_BRIDGE_POLICY_HPP

