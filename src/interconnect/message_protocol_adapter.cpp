#include "interconnect/message_protocol_adapter.hpp"

#include <cctype>
#include <cstring>
#include <string>

#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_codec.hpp"

namespace vr {
namespace interconnect {
namespace {

constexpr const char* kProtocolPrefix = "pm=";
constexpr const char* kVersionPrefix = "pv=";
constexpr const char* kProtocolSep = ";";
constexpr const char* kWireProtoV2Prefix = "PBV2;";
constexpr const char* kWireCborV2Prefix = "CBV2;";
constexpr std::uint32_t kWireVersionLegacy = 1U;
constexpr std::uint32_t kWireVersionCompact = 1U;
constexpr std::uint32_t kWireVersionProtobuf = 2U;
constexpr std::uint32_t kWireVersionCbor = 2U;

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

// 解析协议声明头：兼容两种格式
// 1) 新格式：pm=<mode>;pv=<version>;<payload>
// 2) 旧格式：pm=<mode>;<payload>
// 解析失败时返回 false，由上层统一按无效报文处理。
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

    const std::size_t first_sep = text.find(kProtocolSep);
    if (first_sep == std::string::npos || first_sep <= 3U) {
        return false;
    }

    const std::string mode_token = text.substr(3U, first_sep - 3U);
    *declared_mode = ParseProtocolTag(mode_token);

    const std::size_t second_sep = text.find(kProtocolSep, first_sep + 1U);
    if (second_sep == std::string::npos) {
        *body = text.substr(first_sep + 1U);
        return true;
    }

    const std::string version_token = text.substr(first_sep + 1U, second_sep - first_sep - 1U);
    if (version_token.rfind(kVersionPrefix, 0) != 0) {
        return false;
    }

    *body = text.substr(second_sep + 1U);
    return true;
}

bool ParseWireFrame(const std::string& body,
                    const char* prefix,
                    std::string* payload) {
    if (payload == nullptr || body.rfind(prefix, 0) != 0) {
        return false;
    }

    const std::size_t len_sep = body.find(kProtocolSep, std::strlen(prefix));
    if (len_sep == std::string::npos) {
        return false;
    }

    const std::string len_token = body.substr(std::strlen(prefix), len_sep - std::strlen(prefix));
    if (len_token.empty() ||
        !std::all_of(len_token.begin(), len_token.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return false;
    }

    const std::size_t expected_len = static_cast<std::size_t>(std::stoul(len_token));
    const std::string frame_payload = body.substr(len_sep + 1U);
    if (frame_payload.size() != expected_len) {
        return false;
    }

    *payload = frame_payload;
    return true;
}

std::string BuildWireFrame(const char* prefix, const std::string& payload) {
    return std::string(prefix) + std::to_string(payload.size()) + kProtocolSep + payload;
}

std::string WrapEnvelopeHeader(const MessageProtocolMode mode, const std::string& payload) {
    const std::uint32_t version = ProtocolWireVersion(mode);
    return std::string(kProtocolPrefix) + ProtocolTag(mode) + kProtocolSep +
           kVersionPrefix + std::to_string(version) + kProtocolSep + payload;
}

}  // namespace

std::uint32_t ProtocolWireVersion(const MessageProtocolMode mode) noexcept {
    switch (mode) {
        case MessageProtocolMode::kLegacyPipe:
            return kWireVersionLegacy;
        case MessageProtocolMode::kCompact:
            return kWireVersionCompact;
        case MessageProtocolMode::kProtobufReserved:
            return kWireVersionProtobuf;
        case MessageProtocolMode::kCborReserved:
            return kWireVersionCbor;
        default:
            return kWireVersionLegacy;
    }
}

// 按协议模式编码：统一出口，便于未来替换为真实 protobuf/cbor 库实现。
// 当前 protobuf/cbor 走最小可用 wire（PB|/CB|）路径，同时保留头部声明与版本字段。
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
        case MessageProtocolMode::kProtobufReserved: {
            std::string compact;
            ec = MessageCodec::EncodeCompact(envelope, &compact);
            if (ec == vr::core::ErrorCode::kOk) {
                encoded_body = BuildWireFrame(kWireProtoV2Prefix, compact);
            }
            break;
        }
        case MessageProtocolMode::kCborReserved: {
            std::string compact;
            ec = MessageCodec::EncodeCompact(envelope, &compact);
            if (ec == vr::core::ErrorCode::kOk) {
                encoded_body = BuildWireFrame(kWireCborV2Prefix, compact);
            }
            break;
        }
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
                return MessageCodec::Decode(body, out);
            case MessageProtocolMode::kProtobufReserved: {
                std::string payload;
                if (!ParseWireFrame(body, kWireProtoV2Prefix, &payload)) {
                    return vr::core::ErrorCode::kInvalidParam;
                }
                return MessageCodec::Decode(payload, out);
            }
            case MessageProtocolMode::kCborReserved: {
                std::string payload;
                if (!ParseWireFrame(body, kWireCborV2Prefix, &payload)) {
                    return vr::core::ErrorCode::kInvalidParam;
                }
                return MessageCodec::Decode(payload, out);
            }
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
