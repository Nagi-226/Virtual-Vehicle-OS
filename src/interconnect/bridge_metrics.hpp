#ifndef VR_INTERCONNECT_BRIDGE_METRICS_HPP
#define VR_INTERCONNECT_BRIDGE_METRICS_HPP

#include <cstdint>

namespace vr {
namespace interconnect {

struct BridgeMetrics {
    std::uint64_t tx_count{0U};
    std::uint64_t tx_fail_count{0U};
    std::uint64_t rx_count{0U};
    std::uint64_t encode_fail_count{0U};
    std::uint64_t decode_fail_count{0U};
    std::uint64_t expired_drop_count{0U};
    std::uint64_t route_miss_count{0U};
    std::uint64_t invalid_envelope_count{0U};
    std::uint64_t transport_error_count{0U};
    std::uint64_t backpressure_drop_count{0U};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_BRIDGE_METRICS_HPP
