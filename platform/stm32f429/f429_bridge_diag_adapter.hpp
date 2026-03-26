#ifndef VR_PLATFORM_STM32F429_F429_BRIDGE_DIAG_ADAPTER_HPP
#define VR_PLATFORM_STM32F429_F429_BRIDGE_DIAG_ADAPTER_HPP

#include <cstdint>
#include <string>

namespace vr {
namespace interconnect {
class DiagnosticsManager;
}
}

namespace vr {
namespace platform {

struct F429SramDiagEvent {
    std::uint32_t code{0U};
    std::string audit_summary;
};

class F429BridgeDiagAdapter {
public:
    static bool EmitSramBringupEvent(const vr::interconnect::DiagnosticsManager& diagnostics,
                                     const std::string& snapshot_path,
                                     std::uint32_t snapshot_limit,
                                     const F429SramDiagEvent& event);
};

}  // namespace platform
}  // namespace vr

#endif
