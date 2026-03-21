#include "interconnect/message_protocol_adapter.hpp"

#include <string>

#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_codec.hpp"

namespace vr {
namespace interconnect {
namespace {

constexpr const char* kProtocolPrefix = "pm=";
constexpr const char* kProtocolSep = ";";

std::string ProtocolTag(const MessageProtocolMode mode) {
    switch (mode) {
        case MessageProtocolMode::kLegacyPipe:
            return "legacy";
        case MessageProtocolMode::kCompact:
            return "compact";
        case MessageProtocolMode::kProtobufReserved:
            return "protobuf";
        case MessageProtocolMode::kCborReserved:
            return "cbor";
        default:
            return "legacy";
    }
}

MessageProtocolMode ParseProtocolTag(const std::string& tag) {
    if (tag == "legacy") {
        return MessageProtocolMode::kLegacyPipe;
    }
    if (tag == "compact") {
        return MessageProtocolMode::kCompact;
    }
    if (tag == "protobuf") {
        return MessageProtocolMode::kProtobufReserved;
    }
    if (tag == "cbor") {
        return MessageProtocolMode::kCborReserved;
    }
    return MessageProtocolMode::kLegacyPipe;
}

bool SplitEnvelopeHeader(const std::string& text,
                         MessageProtocolMode* declared_mode,
                         std::string* body) {
    if (declared_mode == nullptr || body == nullptr) {
        return false;
    }

    if (text.rfind(kProtocolPrefix, 0) != 0) {
        *declared_mode = MessageProtocolMode::kLegacyPipe;
        *body = text;
        return true;
    }

    const std::size_t sep = text.find(kProtocolSep);
    if (sep == std::string::npos || sep <= 3U) {
        return false;
    }

    const std::string mode_token = text.substr(3U, sep - 3U);
    *declared_mode = ParseProtocolTag(mode_token);
    *body = text.substr(sep + 1U);
    return true;
}

std::string WrapEnvelopeHeader(const MessageProtocolMode mode, const std::string& payload) {
    return std::string(kProtocolPrefix) + ProtocolTag(mode) + kProtocolSep + payload;
}

}  // namespace

vr::core::ErrorCode EncodeByProtocol(const MessageProtocolMode mode,
                                     const MessageEnvelope& envelope,
                                     std::string* out) noexcept {
    if (out == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::string encoded_body;
    vr::core::ErrorCode ec = vr::core::ErrorCode::kInvalidParam;

    switch (mode) {
        case MessageProtocolMode::kLegacyPipe:
            ec = MessageCodec::Encode(envelope, &encoded_body);
            break;
        case MessageProtocolMode::kCompact:
            ec = MessageCodec::EncodeCompact(envelope, &encoded_body);
            break;
        case MessageProtocolMode::kProtobufReserved:
            ec = MessageCodec::EncodeCompact(envelope, &encoded_body);
            break;
        case MessageProtocolMode::kCborReserved:
            ec = MessageCodec::EncodeCompact(envelope, &encoded_body);
            break;
        default:
            return vr::core::ErrorCode::kInvalidParam;
    }

    if (ec != vr::core::ErrorCode::kOk) {
        return ec;
    }

    *out = WrapEnvelopeHeader(mode, encoded_body);
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode DecodeByProtocol(const MessageProtocolMode mode,
                                     const std::string& text,
                                     MessageEnvelope* out) noexcept {
    if (out == nullptr || text.empty()) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    MessageProtocolMode declared_mode = MessageProtocolMode::kLegacyPipe;
    std::string body;
    if (!SplitEnvelopeHeader(text, &declared_mode, &body)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    // downgrade strategy: decode with declared mode first, fallback to receiver mode.
    const MessageProtocolMode first_try = declared_mode;
    const MessageProtocolMode second_try = mode;

    auto decode_once = [&](const MessageProtocolMode m) {
        switch (m) {
            case MessageProtocolMode::kLegacyPipe:
            case MessageProtocolMode::kCompact:
            case MessageProtocolMode::kProtobufReserved:
            case MessageProtocolMode::kCborReserved:
                return MessageCodec::Decode(body, out);
            default:
                return vr::core::ErrorCode::kInvalidParam;
        }
    };

    vr::core::ErrorCode ec = decode_once(first_try);
    if (ec == vr::core::ErrorCode::kOk) {
        return ec;
    }

    if (second_try != first_try) {
        ec = decode_once(second_try);
    }
    return ec;
}

bool SelfCheckProtocolCapability(const MessageProtocolMode mode) noexcept {
    MessageEnvelope envelope;
    envelope.source = "selfcheck";
    envelope.target = "selfcheck";
    envelope.topic = "protocol.selfcheck";
    envelope.payload = "ping";

    std::string encoded;
    if (EncodeByProtocol(mode, envelope, &encoded) != vr::core::ErrorCode::kOk) {
        return false;
    }

    MessageEnvelope decoded;
    if (DecodeByProtocol(mode, encoded, &decoded) != vr::core::ErrorCode::kOk) {
        return false;
    }

    return decoded.topic == envelope.topic && decoded.payload == envelope.payload;
}

}  // namespace interconnect
}  // namespace vr
