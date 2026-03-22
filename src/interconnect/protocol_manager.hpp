#ifndef VR_INTERCONNECT_PROTOCOL_MANAGER_HPP
#define VR_INTERCONNECT_PROTOCOL_MANAGER_HPP

#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

class ProtocolManager {
public:
    MessageProtocolMode ResolveMode(const BridgeConfig& config,
                                    const MessageEnvelope& envelope) const;

    vr::core::ErrorCode Encode(const MessageProtocolMode mode,
                               const MessageEnvelope& envelope,
                               std::string* out) const noexcept;

    vr::core::ErrorCode Decode(const MessageProtocolMode mode,
                               const std::string& text,
                               MessageEnvelope* out) const noexcept;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_PROTOCOL_MANAGER_HPP
