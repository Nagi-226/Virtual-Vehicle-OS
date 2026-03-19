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

bool TestCodecCapabilitiesAndSkeletons() {
    vr::interconnect::MessageEnvelope msg;
    msg.schema_version = 1;
    msg.channel = vr::interconnect::ChannelType::kTelemetry;
    msg.qos = vr::interconnect::DeliveryQoS::kBestEffort;
    msg.sequence = 7;
    msg.timestamp_ms = 77;
    msg.ttl_ms = 88;
    msg.topic = "probe.topic";
    msg.payload = "probe_payload";

    std::string pb;
    std::string cbor;
    const auto pb_ec = vr::interconnect::MessageCodec::EncodeProtobufSkeleton(msg, &pb);
    const auto cbor_ec = vr::interconnect::MessageCodec::EncodeCborSkeleton(msg, &cbor);
    const auto caps = vr::interconnect::MessageCodec::ProbeCapabilities();

    return ExpectTrue(pb_ec == vr::core::ErrorCode::kOk, "protobuf skeleton encode") &&
           ExpectTrue(cbor_ec == vr::core::ErrorCode::kOk, "cbor skeleton encode") &&
           ExpectTrue(pb.find("pb_skel:") == 0, "protobuf skeleton prefix") &&
           ExpectTrue(cbor.find("cbor_skel:") == 0, "cbor skeleton prefix") &&
           ExpectTrue(caps.legacy_supported, "legacy capability") &&
           ExpectTrue(caps.compact_supported, "compact capability") &&
           ExpectTrue(caps.protobuf_supported, "protobuf capability") &&
           ExpectTrue(caps.cbor_supported, "cbor capability");
}

}  // namespace

int main() {
    const bool ok = TestCodecRoundTrip() && TestCodecCapabilitiesAndSkeletons();
    if (!ok) {
        std::cerr << "interconnect codec test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect codec test passed." << std::endl;
    return 0;
}

