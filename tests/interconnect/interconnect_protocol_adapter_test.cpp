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

bool TestWireFrameLengthValidation() {
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
    if (!TestProtocolSelfCheck() || !TestEncodeDecodeByProtocol() ||
        !TestWireFrameLengthValidation() || !TestDecodeDowngradeCompatibility()) {
        std::cerr << "interconnect protocol adapter test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect protocol adapter test passed." << std::endl;
    return 0;
}
