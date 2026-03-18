#ifndef VR_INTERCONNECT_MESSAGE_AUTHENTICATOR_HPP
#define VR_INTERCONNECT_MESSAGE_AUTHENTICATOR_HPP

#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

class IMessageAuthenticator {
public:
    virtual ~IMessageAuthenticator() = default;
    virtual bool Validate(const MessageEnvelope& envelope) const noexcept = 0;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_MESSAGE_AUTHENTICATOR_HPP
