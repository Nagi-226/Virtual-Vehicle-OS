#include <iostream>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/interconnect_bridge.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/message_protocol_adapter.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestProtocolSelfCheck() {
    return ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                          vr::interconnect::MessageProtocolMode::kLegacyPipe),
                      "legacy selfcheck") &&
           ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                          vr::interconnect::MessageProtocolMode::kCompact),
                      "compact selfcheck") &&
           ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                          vr::interconnect::MessageProtocolMode::kProtobufReserved),
                      "protobuf skeleton selfcheck") &&
           ExpectTrue(vr::interconnect::SelfCheckProtocolCapability(
                          vr::interconnect::MessageProtocolMode::kCborReserved),
                      "cbor skeleton selfcheck");
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

    vr::interconnect::MessageEnvelope out;
    const auto dec = vr::interconnect::DecodeByProtocol(
        vr::interconnect::MessageProtocolMode::kCompact, encoded, &out);

    return ExpectTrue(dec == vr::core::ErrorCode::kOk, "decode by protocol") &&
           ExpectTrue(out.topic == msg.topic, "topic should match") &&
           ExpectTrue(out.payload == msg.payload, "payload should match");
}

}  // namespace

int main() {
    if (!TestProtocolSelfCheck() || !TestEncodeDecodeByProtocol()) {
        std::cerr << "interconnect protocol adapter test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect protocol adapter test passed." << std::endl;
    return 0;
}
