#ifndef VR_INTERCONNECT_BRIDGE_POLICY_HPP
#define VR_INTERCONNECT_BRIDGE_POLICY_HPP

#include <cstdint>
#include <optional>
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
    // max_retries 用 std::optional 区分"没配置"与"显式配置为 0"：
    //   nullopt（默认）= 未配置，归一化时可以从 sla_policy 继承；
    //   has_value()（哪怕值是 0）= 已配置，归一化必须原样保留。
    // 为什么不用哨兵值或 bool 标志：普通赋值 std::optional<int> = 0 天然是
    // "已配置"，类型系统保证赋值即置位，不存在"忘了同时设标志位"的失效模式；
    // 而 -1 之类的哨兵无法与既有代码里合法的负值区分。
    // 这条修复对应"运维把默认策略配成不重试却不生效"的产品 bug。
    std::optional<std::int32_t> max_retries{};
    std::int64_t initial_backoff_ms{10};
    std::int64_t max_backoff_ms{200};
};

struct BridgeSlaPolicy {
    // 策略自身的优先级（数值越大越优先）。与 PolicyRule::priority 语义一致，
    // 但作用于"这条 SLA 策略在被选中执行时应当获得的调度优先级"。
    // 默认 0 表示未显式指定优先级，沿用桥的 receive_priority 基准。
    // 该字段被 ExportPolicyEffectiveView() / ComputePolicySignature() /
    // FormatRuleSummary() 使用，属于对外可观测字段，不可随意删除。
    std::uint32_t priority{0U};
    std::uint32_t max_end_to_end_latency_ms{100};
    std::int64_t transport_receive_timeout_ms{50};
    std::int64_t transport_send_timeout_ms{50};
    BackpressurePolicy backpressure_policy{BackpressurePolicy::kReject};
    DropPolicy drop_policy{DropPolicy::kDropNone};
    RetryBudget retry_budget{};
    bool enable_timeout_sleep{true};
    std::uint32_t receive_timeout_sleep_ms{10U};
    bool lock_policy{false};
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

