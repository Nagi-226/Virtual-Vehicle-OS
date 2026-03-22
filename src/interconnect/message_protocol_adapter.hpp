#ifndef VR_INTERCONNECT_MESSAGE_PROTOCOL_ADAPTER_HPP
#define VR_INTERCONNECT_MESSAGE_PROTOCOL_ADAPTER_HPP

#include <cstdint>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

enum class MessageProtocolMode : std::uint8_t;

vr::core::ErrorCode EncodeByProtocol(MessageProtocolMode mode,
                                     const MessageEnvelope& envelope,
                                     std::string* out) noexcept;

vr::core::ErrorCode DecodeByProtocol(MessageProtocolMode mode,
                                     const std::string& text,
                                     MessageEnvelope* out) noexcept;

std::uint32_t ProtocolWireVersion(MessageProtocolMode mode) noexcept;

bool SelfCheckProtocolCapability(MessageProtocolMode mode) noexcept;

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_MESSAGE_PROTOCOL_ADAPTER_HPP
