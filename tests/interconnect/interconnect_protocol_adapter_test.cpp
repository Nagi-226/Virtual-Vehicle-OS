#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/message_protocol_adapter.hpp"
#include "interconnect/transport.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestProtocolSelfCheck() {
    const bool implemented =
        ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                       vr::interconnect::MessageProtocolMode::kLegacyPipe),
                   "legacy selfcheck") &&
        ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                       vr::interconnect::MessageProtocolMode::kCompact),
                   "compact selfcheck");

    // kProtobufReserved / kCborReserved 是保留位，未实现：
    // EncodeByProtocol() 对它们只是"compact 文本 + PBV2;/CBV2; 长度前缀"，
    // 不含任何 protobuf/CBOR 字节。原断言写的是"必须为真"，那正是被判定为
    // 假绿灯的地方（本端 encode 再本端 decode 的自证对任何对称假实现恒为真）。
    // 这里改为校验其真实状态：能力自检必须报 false。
    const bool reserved_not_claimed =
        ExpectTrue(!vr::interconnect::SelfCheckProtocolCapability(
                       vr::interconnect::MessageProtocolMode::kProtobufReserved),
                   "protobuf is not implemented: capability must be false") &&
        ExpectTrue(!vr::interconnect::SelfCheckProtocolCapability(
                       vr::interconnect::MessageProtocolMode::kCborReserved),
                   "cbor is not implemented: capability must be false");

    return implemented && reserved_not_claimed;
}

// BridgeMetrics 对外广播的协议能力位必须与自检结论一致，否则监控侧会看到
// "已支持 CBOR / protobuf" 的假信号。
bool TestBridgeMetricsProtocolCapability() {
    class NoopTransport final : public vr::interconnect::ITransport {
    public:
        vr::core::ErrorCode Create(
            const vr::interconnect::TransportEndpointConfig& config) noexcept override {
            (void)config;
            return vr::core::ErrorCode::kOk;
        }
        vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                            std::int64_t timeout_ms) noexcept override {
            (void)message;
            (void)priority;
            (void)timeout_ms;
            return vr::core::ErrorCode::kOk;
        }
        vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                               std::int64_t timeout_ms) noexcept override {
            (void)message;
            (void)priority;
            (void)timeout_ms;
            return vr::core::ErrorCode::kTimeout;
        }
        vr::core::ErrorCode DiscardOldest() noexcept override {
            return vr::core::ErrorCode::kOk;
        }
        const char* Name() const noexcept override { return "test.noop"; }
        vr::interconnect::TransportCapabilities Caps() const noexcept override {
            return vr::interconnect::TransportCapabilities{};
        }
        void Close() noexcept override {}
        void Unlink() noexcept override {}
    };

    vr::interconnect::InterconnectBridge bridge(std::make_unique<NoopTransport>(),
                                                std::make_unique<NoopTransport>());
    const auto metrics = bridge.GetBridgeMetrics();

    return ExpectTrue(metrics.protocol_cap_legacy == 1U,
                      "legacy capability should be 1") &&
           ExpectTrue(metrics.protocol_cap_compact == 1U,
                      "compact capability should be 1") &&
           ExpectTrue(metrics.protocol_cap_protobuf == 0U,
                      "protobuf capability must be 0 (not implemented)") &&
           ExpectTrue(metrics.protocol_cap_cbor == 0U,
                      "cbor capability must be 0 (not implemented)");
}

bool TestEncodeDecodeByProtocol() {
    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "protocol.adapter";
    msg.payload = "payload";

    std::string encoded;
    const auto ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kCompact, msg, &encoded);
    if (!ExpectTrue(ec == vr::core::ErrorCode::kOk, "encode by protocol")) {
        return false;
    }

    std::string protobuf_encoded;
    const auto pb_ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, msg, &protobuf_encoded);

    std::string cbor_encoded;
    const auto cb_ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kCborReserved, msg, &cbor_encoded);

    return ExpectTrue(encoded.rfind("pm=compact;pv=1;", 0) == 0,
                      "encoded header should include mode/version") &&
           ExpectTrue(pb_ec == vr::core::ErrorCode::kOk, "protobuf encode should succeed") &&
           ExpectTrue(protobuf_encoded.find("PBV2;") != std::string::npos,
                      "protobuf path should include PBV2 marker") &&
           ExpectTrue(cb_ec == vr::core::ErrorCode::kOk, "cbor encode should succeed") &&
           ExpectTrue(cbor_encoded.find("CBV2;") != std::string::npos,
                      "cbor path should include CBV2 marker") &&
           ExpectTrue(vr::interconnect::ProtocolWireVersion(
                          vr::interconnect::MessageProtocolMode::kProtobufReserved) == 2U,
                      "protobuf wire version should be 2") &&
           ExpectTrue(vr::interconnect::ProtocolWireVersion(
                          vr::interconnect::MessageProtocolMode::kCborReserved) == 2U,
                      "cbor wire version should be 2");
}

bool TestWireFrameLengthValidation() {
    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "protocol.length";
    msg.payload = "payload";

    std::string encoded;
    const auto ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, msg, &encoded);
    if (!ExpectTrue(ec == vr::core::ErrorCode::kOk, "encode protobuf for wire length test")) {
        return false;
    }

    const std::size_t marker = encoded.find("PBV2;");
    if (!ExpectTrue(marker != std::string::npos, "PBV2 marker should exist")) {
        return false;
    }

    std::string tampered = encoded;
    tampered.replace(marker, 8, "PBV2;999;");

    vr::interconnect::MessageEnvelope out;
    const auto dec = vr::interconnect::DecodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, tampered, &out);

    return ExpectTrue(dec != vr::core::ErrorCode::kOk,
                      "tampered wire length should fail decode");
}

// 与上一个用例同名（同样的两处定义曾让本 TU 直接重定义编不过）。
// 这一版按"定位长度 token 再整体替换"的方式篡改长度字段，断言语义更严格
// （要求 kInvalidParam），因此单独命名并单独执行，两条用例都保留。
bool TestWireFrameLengthValidationStrict() {
    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "protocol.length";
    msg.payload = "payload";

    std::string encoded;
    const auto ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, msg, &encoded);
    if (!ExpectTrue(ec == vr::core::ErrorCode::kOk, "encode protobuf for length test")) {
        return false;
    }

    const std::size_t marker = encoded.find("PBV2;");
    if (!ExpectTrue(marker != std::string::npos, "PBV2 marker should exist")) {
        return false;
    }

    std::string tampered = encoded;
    const std::size_t len_start = marker + 5U;
    const std::size_t len_end = tampered.find(';', len_start);
    if (!ExpectTrue(len_end != std::string::npos, "length separator should exist")) {
        return false;
    }
    tampered.replace(len_start, len_end - len_start, "999");

    vr::interconnect::MessageEnvelope out;
    const auto dec = vr::interconnect::DecodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, tampered, &out);

    return ExpectTrue(dec == vr::core::ErrorCode::kInvalidParam,
                      "tampered wire length should fail decode");
}

bool TestDecodeDowngradeCompatibility() {
    vr::interconnect::MessageEnvelope msg;
    msg.source = "vehicle";
    msg.target = "robot";
    msg.topic = "protocol.downgrade";
    msg.payload = "payload";

    std::string encoded;
    const auto ec = vr::interconnect::EncodeByProtocol(
        vr::interconnect::MessageProtocolMode::kProtobufReserved, msg, &encoded);
    if (!ExpectTrue(ec == vr::core::ErrorCode::kOk, "encode protobuf reserved")) {
        return false;
    }

    vr::interconnect::MessageEnvelope out;
    const auto dec = vr::interconnect::DecodeByProtocol(
        vr::interconnect::MessageProtocolMode::kLegacyPipe, encoded, &out);

    return ExpectTrue(dec == vr::core::ErrorCode::kOk, "decode downgrade should succeed") &&
           ExpectTrue(out.topic == msg.topic, "topic should match after downgrade") &&
           ExpectTrue(out.payload == msg.payload, "payload should match after downgrade");
}

}  // namespace

int main() {
    if (!TestProtocolSelfCheck() || !TestBridgeMetricsProtocolCapability() ||
        !TestEncodeDecodeByProtocol() || !TestWireFrameLengthValidation() ||
        !TestWireFrameLengthValidationStrict() || !TestDecodeDowngradeCompatibility()) {
        std::cerr << "interconnect protocol adapter test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect protocol adapter test passed." << std::endl;
    return 0;
}
