#include "interconnect/message_codec.hpp"

#include <array>
#include <sstream>
#include <vector>

namespace vr {
namespace interconnect {
namespace {

constexpr char kFieldSeparator = '|';
constexpr std::size_t kExpectedFieldCount = 11U;
constexpr std::size_t kExpectedCompactFieldCount = 8U;
constexpr std::uint32_t kSchemaVersionCompatBaseline = 1U;

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

// 数值字段是否"全为 ASCII 十进制数字且非空"。
//
// 为什么需要这一步（而不是直接交给 std::stoul）：
//   std::stoul 只解析**前缀**，不检查是否消费完整个字段。于是
//     "0x1F"  -> 读到 'x' 停止 -> 返回 0
//     "42abc" -> 读到 'a' 停止 -> 返回 42
//     "+5"    -> 接受正号    -> 返回 5
//   三者都不会抛异常，解码"成功"却得到一个**静默错误的值**。
// 后果不是崩溃，而是归因错位：sequence 被吞成 0 会让幂等 key（topic+sequence）
// 退化、多条不同消息撞同一 key 而被当重复丢掉；timestamp/ttl 被吞成 0 会让消息
// 必然过期、以 "expired" 的名义被丢弃 —— 排障的人会去查 TTL 策略而不是编码器。
// 这里选择"全数字"而不是"pos == size"：后者挡不住 "+5" 与前导空格。
bool IsDecimalDigits(const std::string& input) {
    if (input.empty()) {
        return false;
    }
    for (const char ch : input) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

// 按分隔符切分并处理转义：保证 payload/topic 中包含 '|' 时仍可安全还原。
// 这是 legacy/compact 文本协议正确性的关键基础能力。
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

vr::core::ErrorCode MessageCodec::EncodeCompact(const MessageEnvelope& envelope,
                                                std::string* out) noexcept {
    if (out == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::ostringstream oss;
    oss << envelope.schema_version << kFieldSeparator << ChannelToString(envelope.channel)
        << kFieldSeparator << QoSToString(envelope.qos) << kFieldSeparator << envelope.sequence
        << kFieldSeparator << envelope.timestamp_ms << kFieldSeparator << envelope.ttl_ms
        << kFieldSeparator << EscapeField(envelope.topic) << kFieldSeparator
        << EscapeField(envelope.payload);

    *out = oss.str();
    return vr::core::ErrorCode::kOk;
}

// 解码入口：同时兼容完整字段与 compact 字段。
// 关键防护：
// - 字段数不匹配直接拒收；
// - schema 低于兼容基线直接拒收；
// - channel/qos 枚举非法直接拒收。
vr::core::ErrorCode MessageCodec::Decode(const std::string& text, MessageEnvelope* out) noexcept {
    if (out == nullptr || text.empty()) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::vector<std::string> fields;
    SplitEscaped(text, &fields);
    if (fields.size() != kExpectedFieldCount && fields.size() != kExpectedCompactFieldCount) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    // 四个数值字段（schema_version / sequence / timestamp_ms / ttl_ms）必须先做
    // "全为十进制数字"的显式校验 —— 否则 std::stoul 的部分消费会让畸形输入静默通过。
    // 见 IsDecimalDigits() 上方的说明。
    if (!IsDecimalDigits(fields[0]) || !IsDecimalDigits(fields[3]) ||
        !IsDecimalDigits(fields[4]) || !IsDecimalDigits(fields[5])) {
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

    if (parsed.schema_version < kSchemaVersionCompatBaseline) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (!ChannelFromString(fields[1], &parsed.channel)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (!QoSFromString(fields[2], &parsed.qos)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (fields.size() == kExpectedCompactFieldCount) {
        parsed.topic = UnescapeField(fields[6]);
        parsed.payload = UnescapeField(fields[7]);
        *out = parsed;
        return vr::core::ErrorCode::kOk;
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

