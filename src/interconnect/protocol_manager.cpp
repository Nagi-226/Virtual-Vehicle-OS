#include "interconnect/protocol_manager.hpp"

#include "interconnect/message_protocol_adapter.hpp"

namespace vr {
namespace interconnect {

MessageProtocolMode ProtocolManager::ResolveMode(const BridgeConfig& config,
                                                 const MessageEnvelope& envelope) const {
    if (config.policy_table.default_policy.lock_policy) {
        return MessageProtocolMode::kCompact;
    }

    if (config.protocol_canary_topic_prefix.empty()) {
        return config.protocol_mode;
    }

    if (envelope.topic.rfind(config.protocol_canary_topic_prefix, 0) != 0) {
        return MessageProtocolMode::kCompact;
    }

    const std::uint32_t gate = static_cast<std::uint32_t>(
        envelope.timestamp_ms == 0U ? 0U : (envelope.timestamp_ms % 100U));
    if (gate < config.protocol_canary_percent) {
        return config.protocol_mode;
    }

    return MessageProtocolMode::kCompact;
}

vr::core::ErrorCode ProtocolManager::Encode(const MessageProtocolMode mode,
                                            const MessageEnvelope& envelope,
                                            std::string* out) const noexcept {
    return EncodeByProtocol(mode, envelope, out);
}

vr::core::ErrorCode ProtocolManager::Decode(const MessageProtocolMode mode,
                                            const std::string& text,
                                            MessageEnvelope* out) const noexcept {
    return DecodeByProtocol(mode, text, out);
}

}  // namespace interconnect
}  // namespace vr
