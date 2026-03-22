#ifndef VR_INTERCONNECT_DIAGNOSTICS_MANAGER_HPP
#define VR_INTERCONNECT_DIAGNOSTICS_MANAGER_HPP

#include <string>

#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

class DiagnosticsManager {
public:
    std::string BuildUnknownCommandResult() const;
    void RecordSnapshot(const std::string& path,
                        std::uint32_t limit,
                        const std::string& event,
                        const MessageEnvelope* envelope) const;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_DIAGNOSTICS_MANAGER_HPP
