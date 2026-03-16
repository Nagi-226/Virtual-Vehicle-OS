#include <iostream>
#include <memory>
#include <string>

#include "core/error_code.hpp"
#include "interconnect/message_codec.hpp"
#include "interconnect/message_envelope.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

bool TestEncodeDecodeConsistency() {
    vr::interconnect::MessageEnvelope envelope;
    envelope.source = "vehicle";
    envelope.target = "robot";
    envelope.topic = "vehicle.telemetry";
    envelope.trace_id = "trace_consistency";
    envelope.channel = vr::interconnect::ChannelType::kTelemetry;
    envelope.qos = vr::interconnect::DeliveryQoS::kBestEffort;
    envelope.payload = "payload";
    envelope.timestamp_ms = 123U;
    envelope.ttl_ms = 200U;

    std::string encoded;
    const auto encode_ec = vr::interconnect::MessageCodec::Encode(envelope, &encoded);
    if (!ExpectTrue(encode_ec == vr::core::ErrorCode::kOk, "encode should succeed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope decoded;
    const auto decode_ec = vr::interconnect::MessageCodec::Decode(encoded, &decoded);
    if (!ExpectTrue(decode_ec == vr::core::ErrorCode::kOk, "decode should succeed")) {
        return false;
    }

    return ExpectTrue(decoded.source == envelope.source, "source should match") &&
           ExpectTrue(decoded.target == envelope.target, "target should match") &&
           ExpectTrue(decoded.topic == envelope.topic, "topic should match") &&
           ExpectTrue(decoded.trace_id == envelope.trace_id, "trace id should match") &&
           ExpectTrue(decoded.channel == envelope.channel, "channel should match") &&
           ExpectTrue(decoded.qos == envelope.qos, "qos should match") &&
           ExpectTrue(decoded.payload == envelope.payload, "payload should match") &&
           ExpectTrue(decoded.timestamp_ms == envelope.timestamp_ms, "timestamp should match") &&
           ExpectTrue(decoded.ttl_ms == envelope.ttl_ms, "ttl should match");
}

}  // namespace

int main() {
    if (!TestEncodeDecodeConsistency()) {
        std::cerr << "interconnect transport consistency test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect transport consistency test passed." << std::endl;
    return 0;
}
