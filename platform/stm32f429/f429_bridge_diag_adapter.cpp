#include "f429_bridge_diag_adapter.hpp"

#include <unordered_map>

#include "interconnect/diagnostics_manager.hpp"

namespace vr {
namespace platform {

bool F429BridgeDiagAdapter::EmitSramBringupEvent(
    const vr::interconnect::DiagnosticsManager& diagnostics,
    const std::string& snapshot_path,
    const std::uint32_t snapshot_limit,
    const F429SramDiagEvent& event) {
    std::unordered_map<std::string, std::string> fields;
    fields["platform"] = "stm32f429zgt6";
    fields["component"] = "fmc_sram";
    fields["diag_code"] = std::to_string(event.code);
    fields["audit"] = event.audit_summary;

    diagnostics.RecordSnapshotWithFields(snapshot_path,
                                         snapshot_limit,
                                         "platform_fmc_sram_bringup",
                                         fields);
    return true;
}

}  // namespace platform
}  // namespace vr
