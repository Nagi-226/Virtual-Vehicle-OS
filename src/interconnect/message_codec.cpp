#include "interconnect/message_codec.hpp"

#include <array>
#include <sstream>
#include <vector>

namespace vr {
namespace interconnect {
namespace {

constexpr char kFieldSeparator = '|';
constexpr std::size_t kExpectedFieldCount = 11U;

std::string EscapeField(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (char ch : input) {
        if (ch == '\\' || ch == kFieldSeparator) {
            out.push_back('\\');
        }
        out.push_back(ch);
    }

    return out;
}

std::string UnescapeField(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    bool escaped = false;
    for (char ch : input) {
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }

        if (ch == '\\') {
            escaped = true;
            continue;
        }

        out.push_back(ch);
    }

    if (escaped) {
        out.push_back('\\');
    }

    return out;
}

void SplitEscaped(const std::string& text, std::vector<std::string>* fields) {
    fields->clear();

    std::string current;
    current.reserve(text.size());

    std::size_t backslash_run = 0U;
    for (char ch : text) {
        const bool separator_escaped = (ch == kFieldSeparator) && ((backslash_run % 2U) == 1U);
        if (ch == kFieldSeparator && !separator_escaped) {
            fields->push_back(current);
            current.clear();
            backslash_run = 0U;
            continue;
        }

        current.push_back(ch);

        if (ch == '\\') {
            ++backslash_run;
        } else {
            backslash_run = 0U;
        }
    }

    fields->push_back(current);
}

std::string ChannelToString(const ChannelType channel) {
    switch (channel) {
        case ChannelType::kControl:
            return "control";
        case ChannelType::kTelemetry:
            return "telemetry";
        case ChannelType::kEvent:
            return "event";
        default:
            return "event";
    }
}

bool ChannelFromString(const std::string& value, ChannelType* channel) {
    if (value == "control") {
        *channel = ChannelType::kControl;
        return true;
    }
    if (value == "telemetry") {
        *channel = ChannelType::kTelemetry;
        return true;
    }
    if (value == "event") {
        *channel = ChannelType::kEvent;
        return true;
    }
    return false;
}

std::string QoSToString(const DeliveryQoS qos) {
    switch (qos) {
        case DeliveryQoS::kBestEffort:
            return "best_effort";
        case DeliveryQoS::kAtLeastOnce:
            return "at_least_once";
        default:
            return "best_effort";
    }
}

bool QoSFromString(const std::string& value, DeliveryQoS* qos) {
    if (value == "best_effort") {
        *qos = DeliveryQoS::kBestEffort;
        return true;
    }
    if (value == "at_least_once") {
        *qos = DeliveryQoS::kAtLeastOnce;
        return true;
    }
    return false;
}

}  // namespace

vr::core::ErrorCode MessageCodec::Encode(const MessageEnvelope& envelope, std::string* out) noexcept {
    if (out == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::ostringstream oss;
    oss << envelope.schema_version << kFieldSeparator << ChannelToString(envelope.channel)
        << kFieldSeparator << QoSToString(envelope.qos) << kFieldSeparator << envelope.sequence
        << kFieldSeparator << envelope.timestamp_ms << kFieldSeparator << envelope.ttl_ms
        << kFieldSeparator << EscapeField(envelope.source) << kFieldSeparator
        << EscapeField(envelope.target) << kFieldSeparator << EscapeField(envelope.topic)
        << kFieldSeparator << EscapeField(envelope.trace_id) << kFieldSeparator
        << EscapeField(envelope.payload);

    *out = oss.str();
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode MessageCodec::Decode(const std::string& text, MessageEnvelope* out) noexcept {
    if (out == nullptr || text.empty()) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::vector<std::string> fields;
    SplitEscaped(text, &fields);
    if (fields.size() != kExpectedFieldCount) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    MessageEnvelope parsed;
    try {
        parsed.schema_version = static_cast<std::uint32_t>(std::stoul(fields[0]));
        parsed.sequence = static_cast<std::uint64_t>(std::stoull(fields[3]));
        parsed.timestamp_ms = static_cast<std::uint64_t>(std::stoull(fields[4]));
        parsed.ttl_ms = static_cast<std::uint32_t>(std::stoul(fields[5]));
    } catch (...) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (!ChannelFromString(fields[1], &parsed.channel)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (!QoSFromString(fields[2], &parsed.qos)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    parsed.source = UnescapeField(fields[6]);
    parsed.target = UnescapeField(fields[7]);
    parsed.topic = UnescapeField(fields[8]);
    parsed.trace_id = UnescapeField(fields[9]);
    parsed.payload = UnescapeField(fields[10]);

    *out = parsed;
    return vr::core::ErrorCode::kOk;
}

}  // namespace interconnect
}  // namespace vr

