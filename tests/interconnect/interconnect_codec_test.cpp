#include <iostream>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/message_codec.hpp"
#include "interconnect/message_envelope.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestCodecRoundTrip() {
    vr::interconnect::MessageEnvelope msg;
    msg.schema_version = 1;
    msg.channel = vr::interconnect::ChannelType::kControl;
    msg.qos = vr::interconnect::DeliveryQoS::kAtLeastOnce;
    msg.sequence = 42;
    msg.timestamp_ms = 1000;
    msg.ttl_ms = 100;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "vehicle.command";
    msg.trace_id = "trace_42";
    msg.payload = "go|forward\\step";

    std::string encoded;
    const auto encode_ec = vr::interconnect::MessageCodec::Encode(msg, &encoded);
    if (!ExpectTrue(encode_ec == vr::core::ErrorCode::kOk, "Encode should succeed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope decoded;
    const auto decode_ec = vr::interconnect::MessageCodec::Decode(encoded, &decoded);
    if (!ExpectTrue(decode_ec == vr::core::ErrorCode::kOk, "Decode should succeed")) {
        return false;
    }

    return ExpectTrue(decoded.schema_version == msg.schema_version, "schema_version mismatch") &&
           ExpectTrue(decoded.channel == msg.channel, "channel mismatch") &&
           ExpectTrue(decoded.qos == msg.qos, "qos mismatch") &&
           ExpectTrue(decoded.sequence == msg.sequence, "sequence mismatch") &&
           ExpectTrue(decoded.timestamp_ms == msg.timestamp_ms, "timestamp mismatch") &&
           ExpectTrue(decoded.ttl_ms == msg.ttl_ms, "ttl mismatch") &&
           ExpectTrue(decoded.source == msg.source, "source mismatch") &&
           ExpectTrue(decoded.target == msg.target, "target mismatch") &&
           ExpectTrue(decoded.topic == msg.topic, "topic mismatch") &&
           ExpectTrue(decoded.trace_id == msg.trace_id, "trace_id mismatch") &&
           ExpectTrue(decoded.payload == msg.payload, "payload mismatch");
}

// ===========================================================================
// 数值字段必须"全为十进制数字"，否则拒收。
//
// 背景：Decode 原来直接调 std::stoul，而 std::stoul 只解析**前缀**、不检查是否
// 消费完整个字段。于是 "0x1F" -> 0、"42abc" -> 42、"+5" -> 5，三者都不抛异常，
// 解码"成功"却拿到静默错误的值。危害不是崩溃而是**归因错位**：
//   sequence 被吞成 0 -> 幂等 key(topic+sequence) 退化 -> 合法消息被判重复丢弃
//   timestamp/ttl 被吞成 0 -> 消息必然过期 -> 以 "expired" 名义丢弃
//   -> 排障的人会去查 TTL 策略，而真正该查的是编码器
// 现在 Decode 前置 IsDecimalDigits() 校验，以下每一条都必须 kInvalidParam。
// ===========================================================================
bool TestDecodeRejectsMalformedNumericFields() {
    // 8 字段紧凑版；只把被考察的那一个字段换掉。
    struct Case {
        const char* label;
        const char* text;
    };
    const Case cases[] = {
        {"sequence=0x1F", "1|telemetry|best_effort|0x1F|1000|3000|t|p"},
        {"sequence=42abc", "1|telemetry|best_effort|42abc|1000|3000|t|p"},
        {"timestamp=0x1F", "1|telemetry|best_effort|1|0x1F|3000|t|p"},
        {"ttl=0x1F", "1|telemetry|best_effort|1|1000|0x1F|t|p"},
        {"sequence=+5", "1|telemetry|best_effort|+5|1000|3000|t|p"},
        {"sequence= 42", "1|telemetry|best_effort| 42|1000|3000|t|p"},
        {"sequence=", "1|telemetry|best_effort||1000|3000|t|p"},
        {"sequence=-1", "1|telemetry|best_effort|-1|1000|3000|t|p"},
    };

    bool all_ok = true;
    for (const auto& c : cases) {
        vr::interconnect::MessageEnvelope out;
        const auto ec = vr::interconnect::MessageCodec::Decode(c.text, &out);
        all_ok = ExpectTrue(ec == vr::core::ErrorCode::kInvalidParam, c.label) && all_ok;
    }
    return all_ok;
}

// 反向保证：严格化不能把**合法**输入也拒掉（防止过收紧）。
bool TestDecodeStillAcceptsWellFormedFrames() {
    // 紧凑 8 字段，全十进制 —— 必须成功，且字段值正确。
    const std::string compact =
        "1|telemetry|best_effort|42|987654321|3000|vehicle.perception.fatigue|"
        "dv=O;dl=2;ev=O;es=1;mv=N;ms=-;ab=350;dg=1";
    vr::interconnect::MessageEnvelope out;
    const auto ec = vr::interconnect::MessageCodec::Decode(compact, &out);
    if (!ExpectTrue(ec == vr::core::ErrorCode::kOk, "well-formed compact frame should decode")) {
        return false;
    }

    // 前导 0 属于"全数字"，接受（生产者规范建议不发，但解码侧不因此拒收）。
    vr::interconnect::MessageEnvelope padded;
    const auto padded_ec = vr::interconnect::MessageCodec::Decode(
        "1|telemetry|best_effort|007|1000|3000|t|p", &padded);

    return ExpectTrue(out.schema_version == 1U, "schema_version should be 1") &&
           ExpectTrue(out.sequence == 42U, "sequence should be 42") &&
           ExpectTrue(out.timestamp_ms == 987654321U, "timestamp should match") &&
           ExpectTrue(out.ttl_ms == 3000U, "ttl should match") &&
           ExpectTrue(out.topic == "vehicle.perception.fatigue", "topic should match") &&
           ExpectTrue(padded_ec == vr::core::ErrorCode::kOk && padded.sequence == 7U,
                      "leading zeros are still all-digits and must be accepted");
}

}  // namespace

int main() {
    const bool ok = TestCodecRoundTrip() && TestDecodeRejectsMalformedNumericFields() &&
                    TestDecodeStillAcceptsWellFormedFrames();
    if (!ok) {
        std::cerr << "interconnect codec test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect codec test passed." << std::endl;
    return 0;
}

