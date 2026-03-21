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

}  // namespace

int main() {
    const bool ok = TestCodecRoundTrip();
    if (!ok) {
        std::cerr << "interconnect codec test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect codec test passed." << std::endl;
    return 0;
}

