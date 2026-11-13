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
        // 整块继承 sla_policy，但**显式配置过的重试预算必须原样保住**：
        // "整表都用默认值"不能成为覆盖用户显式意图的理由。
        const RetryBudget explicit_retry_budget =
            config->policy_table.default_policy.retry_budget;
        config->policy_table.default_policy = config->sla_policy;
        if (explicit_retry_budget.max_retries.has_value()) {
            config->policy_table.default_policy.retry_budget = explicit_retry_budget;
        }
    }

    // 重试预算的继承条件有两条，缺一不可：
    //   ① 未配置（max_retries 为 nullopt）。这里刻意不再用 `<= 0`：
    //      0 是合法配置（明确表示不重试），旧实现把它当"没配置"，
    //      于是被 sla_policy 的预算覆盖，标称不重试的策略照样会重试一次。
    //   ② 这条策略愿意缓冲/重试（backpressure != kReject）。kReject 的语义是
    //      "现在就把失败告诉调用方"，给它补一个重试预算等于把它变成"重试后再拒"。
    //      注意：只约束"继承"，不约束"显式配置"——显式给出的预算一律照用。
    const bool retry_budget_configured =
        config->policy_table.default_policy.retry_budget.max_retries.has_value();
    const bool policy_wants_buffering =
        config->policy_table.default_policy.backpressure_policy != BackpressurePolicy::kReject;
    if (!retry_budget_configured && policy_wants_buffering) {
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
