#ifndef VR_INTERCONNECT_BRIDGE_METRICS_HPP
#define VR_INTERCONNECT_BRIDGE_METRICS_HPP

#include <cstdint>
#include <string>
#include <unordered_map>

#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

struct BridgeMetrics {
    std::uint64_t tx_count{0U};
    std::uint64_t tx_fail_count{0U};
    std::uint64_t rx_count{0U};
    std::uint64_t encode_fail_count{0U};
    std::uint64_t decode_fail_count{0U};
    std::uint64_t expired_drop_count{0U};
    std::uint64_t route_miss_count{0U};
    std::uint64_t handler_error_count{0U};
    std::uint64_t invalid_envelope_count{0U};
    std::uint64_t transport_error_count{0U};
    std::uint64_t backpressure_drop_count{0U};

    std::uint64_t policy_hit_count{0U};
    std::uint64_t policy_override_count{0U};
    std::uint64_t policy_conflict_count{0U};
    std::uint64_t policy_conflict_sampled_count{0U};
    std::uint64_t policy_cache_hit_count{0U};
    std::uint64_t policy_cache_miss_count{0U};
    std::uint64_t policy_lint_issue_count{0U};
    std::uint64_t policy_lint_warning_count{0U};

    std::uint64_t trace_id_present_count{0U};
    std::uint64_t trace_id_missing_count{0U};
    std::uint64_t trace_id_sampled_count{0U};
    std::uint64_t sla_violation_sampled_count{0U};

    std::uint64_t reload_success_count{0U};
    std::uint64_t reload_fail_count{0U};
    std::uint64_t reload_rollback_count{0U};
    std::uint64_t last_reload_timestamp_ms{0U};
    std::int32_t last_reload_status_code{0};
    std::int32_t reload_rollback_reason_code{0};
    std::uint64_t loaded_config_version{0U};

    std::uint32_t protocol_mode_code{0U};
    std::uint64_t protocol_cap_legacy{1U};
    std::uint64_t protocol_cap_compact{1U};
    std::uint64_t protocol_cap_protobuf{0U};
    std::uint64_t protocol_cap_cbor{0U};

    std::uint64_t configured_transport_pool_size{2U};
    std::uint64_t failover_hit_count{0U};
    std::uint64_t transport_primary_healthy{1U};
    std::uint64_t transport_secondary_healthy{1U};

    std::uint64_t diag_dump_state_count{0U};
    std::uint64_t diag_route_event_count{0U};
    std::uint64_t diag_failover_event_count{0U};

    std::unordered_map<std::string, std::uint64_t> topic_tx_count;
    std::unordered_map<std::string, std::uint64_t> topic_rx_count;
    std::unordered_map<std::string, std::uint64_t> topic_expired_drop_count;

    std::unordered_map<ChannelType, std::uint64_t> channel_tx_count;
    std::unordered_map<ChannelType, std::uint64_t> channel_rx_count;
    std::unordered_map<ChannelType, std::uint64_t> channel_expired_drop_count;

    std::unordered_map<DeliveryQoS, std::uint64_t> qos_tx_count;
    std::unordered_map<DeliveryQoS, std::uint64_t> qos_rx_count;
    std::unordered_map<DeliveryQoS, std::uint64_t> qos_expired_drop_count;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_BRIDGE_METRICS_HPP
