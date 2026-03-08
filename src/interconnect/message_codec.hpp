#ifndef VR_INTERCONNECT_MESSAGE_CODEC_HPP
#define VR_INTERCONNECT_MESSAGE_CODEC_HPP

#include <string>

#include "core/error_code.hpp"
#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

class MessageCodec {
public:
    static vr::core::ErrorCode Encode(const MessageEnvelope& envelope, std::string* out) noexcept;
    static vr::core::ErrorCode Decode(const std::string& text, MessageEnvelope* out) noexcept;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_MESSAGE_CODEC_HPP
