#ifndef VR_INTERCONNECT_MESSAGE_ENVELOPE_HPP
#define VR_INTERCONNECT_MESSAGE_ENVELOPE_HPP

#include <cstdint>
#include <string>

namespace vr {
namespace interconnect {

enum class ChannelType : std::uint8_t {
    kControl = 0,
    kTelemetry = 1,
    kEvent = 2
};

enum class DeliveryQoS : std::uint8_t {
    kBestEffort = 0,
    kAtLeastOnce = 1
};

struct MessageEnvelope {
    std::uint32_t schema_version{1U};
    ChannelType channel{ChannelType::kEvent};
    DeliveryQoS qos{DeliveryQoS::kBestEffort};
    std::uint64_t sequence{0U};
    std::uint64_t timestamp_ms{0U};
    std::uint32_t ttl_ms{1000U};

    std::string source;
    std::string target;
    std::string topic;
    std::string trace_id;
    std::string idempotency_key;
    std::string payload;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_MESSAGE_ENVELOPE_HPP

