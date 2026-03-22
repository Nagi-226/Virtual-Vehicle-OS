#ifndef VR_INTERCONNECT_POLICY_MANAGER_HPP
#define VR_INTERCONNECT_POLICY_MANAGER_HPP

#include <string>

#include "interconnect/bridge_policy.hpp"
#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

class PolicyManager {
public:
    struct ResolveResult {
        BridgeSlaPolicy policy{};
        bool override_applied{false};
        bool conflict_detected{false};
    };

    static void NormalizePolicyDefaults(BridgeConfig* config);

    ResolveResult Resolve(const BridgeConfig& config,
                          const MessageEnvelope& envelope) const noexcept;

    std::string ExportEffectiveView(const BridgeConfig& config,
                                    std::size_t cache_size,
                                    std::uint64_t cache_hit,
                                    std::uint64_t cache_miss,
                                    std::uint64_t conflict_count) const;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_POLICY_MANAGER_HPP
