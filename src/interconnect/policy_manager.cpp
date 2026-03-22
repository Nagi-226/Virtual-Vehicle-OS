#include "interconnect/policy_manager.hpp"

#include <algorithm>
#include <sstream>

namespace vr {
namespace interconnect {

void PolicyManager::NormalizePolicyDefaults(BridgeConfig* config) {
    if (config == nullptr) {
        return;
    }

    if (config->policy_table.rules.empty() &&
        config->policy_table.default_policy.max_end_to_end_latency_ms == 100U &&
        config->policy_table.default_policy.transport_receive_timeout_ms == 50 &&
        config->policy_table.default_policy.transport_send_timeout_ms == 50 &&
        config->policy_table.default_policy.backpressure_policy == BackpressurePolicy::kReject) {
        config->policy_table.default_policy = config->sla_policy;
    }

    if (config->policy_table.default_policy.retry_budget.max_retries <= 0) {
        config->policy_table.default_policy.retry_budget = config->sla_policy.retry_budget;
    }
    if (config->policy_table.default_policy.drop_policy == DropPolicy::kDropNone) {
        config->policy_table.default_policy.drop_policy = config->sla_policy.drop_policy;
    }
    if (config->policy_table.default_policy.drop_policy == DropPolicy::kDropNone &&
        config->policy_table.default_policy.backpressure_policy == BackpressurePolicy::kDropOldest) {
        config->policy_table.default_policy.drop_policy = DropPolicy::kDropOldest;
    }
}

PolicyManager::ResolveResult PolicyManager::Resolve(const BridgeConfig& config,
                                                    const MessageEnvelope& /*envelope*/) const noexcept {
    ResolveResult result;
    result.policy = config.policy_table.default_policy;
    return result;
}

std::string PolicyManager::ExportEffectiveView(const BridgeConfig& config,
                                               const std::size_t cache_size,
                                               const std::uint64_t cache_hit,
                                               const std::uint64_t cache_miss,
                                               const std::uint64_t conflict_count) const {
    std::ostringstream oss;
    oss << "policy_effective_view:{";
    oss << "default_policy_priority=" << config.policy_table.default_policy.priority << ",";
    oss << "rules=" << config.policy_table.rules.size() << ",";
    oss << "templates=" << config.policy_table.template_rules.size() << ",";
    oss << "overrides=" << config.policy_table.runtime_overrides.size() << ",";
    oss << "cache_size=" << cache_size << ",";
    oss << "cache_hits=" << cache_hit << ",";
    oss << "cache_miss=" << cache_miss << ",";
    oss << "policy_conflicts=" << conflict_count;
    oss << "}";
    return oss.str();
}

}  // namespace interconnect
}  // namespace vr
