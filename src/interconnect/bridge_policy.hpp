#ifndef VR_INTERCONNECT_BRIDGE_POLICY_HPP
#define VR_INTERCONNECT_BRIDGE_POLICY_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

enum class BackpressurePolicy : std::uint8_t {
    kReject = 0,
    kDropOldest = 1
};

enum class DropPolicy : std::uint8_t {
    kDropNone = 0,
    kDropNew = 1,
    kDropOldest = 2
};

struct RetryBudget {
    std::int32_t max_retries{0};
    std::int64_t initial_backoff_ms{10};
    std::int64_t max_backoff_ms{200};
};

struct BridgeSlaPolicy {
    std::uint32_t max_end_to_end_latency_ms{100};
    std::int64_t transport_receive_timeout_ms{50};
    std::int64_t transport_send_timeout_ms{50};
    BackpressurePolicy backpressure_policy{BackpressurePolicy::kReject};
    DropPolicy drop_policy{DropPolicy::kDropNone};
    RetryBudget retry_budget{};
    bool enable_timeout_sleep{true};
    std::uint32_t receive_timeout_sleep_ms{10U};
};

struct PolicyRule {
    std::uint32_t priority{0U};

    bool match_any_channel{true};
    ChannelType channel{ChannelType::kEvent};

    bool match_any_source{true};
    std::string source;

    bool match_any_target{true};
    std::string target;

    bool match_any_qos{true};
    DeliveryQoS qos{DeliveryQoS::kBestEffort};

    std::string topic;
    BridgeSlaPolicy policy;
};

struct BridgePolicyTable {
    BridgeSlaPolicy default_policy;

    std::vector<PolicyRule> template_rules;
    std::vector<PolicyRule> rules;
    std::vector<PolicyRule> runtime_overrides;

    std::unordered_map<std::string, std::vector<std::size_t>> template_topic_rule_index;
    std::unordered_map<std::string, std::vector<std::size_t>> topic_rule_index;
    std::unordered_map<std::string, std::vector<std::size_t>> override_topic_rule_index;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_BRIDGE_POLICY_HPP

