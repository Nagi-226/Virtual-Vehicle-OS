#ifndef VR_INTERCONNECT_BRIDGE_CONFIG_HPP
#define VR_INTERCONNECT_BRIDGE_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "core/thread_pool.hpp"
#include "interconnect/bridge_policy.hpp"
#include "interconnect/message_envelope.hpp"
#include "interconnect/transport.hpp"

namespace vr {
namespace interconnect {

// Bridge 报文协议模式。
//
// 头文件拓扑说明：本枚举原先定义在 interconnect_bridge.hpp 中，而
// protocol_manager.hpp 需要它以声明成员函数，interconnect_bridge.hpp 又先
// 包含 protocol_manager.hpp，形成循环包含（诊断见 A1.2）。因此把它下沉到
// 本文件，作为协议层的公共类型仓库：
//   message_envelope.hpp -> transport.hpp -> bridge_policy.hpp -> bridge_config.hpp
//   -> 各 manager 头 -> interconnect_bridge.hpp
//
// 实现状态（重要，勿当能力位使用）：
//   kLegacyPipe / kCompact 是真实实现；
//   kProtobufReserved / kCborReserved 只是**保留位**，当前 wire 编码是
//   "compact 文本 + PBV2;/CBV2; 长度前缀"，一个 protobuf/CBOR 字节都没有。
//   因此 SelfCheckProtocolCapability() 对这两者返回 false，且
//   BridgeMetrics::protocol_cap_protobuf / protocol_cap_cbor 恒为 0。
enum class MessageProtocolMode : std::uint8_t {
    kLegacyPipe = 0,
    kCompact = 1,
    kProtobufReserved = 2,  // 未实现：wire 实际为 compact 文本帧 + PBV2; 前缀
    kCborReserved = 3,      // 未实现：wire 实际为 compact 文本帧 + CBV2; 前缀
};

// Bridge 的完整运行配置。由 IConfigProvider 产出并由 InterconnectBridge 消费。
struct BridgeConfig {
    TransportEndpointConfig vehicle_to_robot_endpoint;
    TransportEndpointConfig robot_to_vehicle_endpoint;
    std::vector<TransportEndpointConfig> additional_endpoints;
    vr::core::ThreadConfig thread_pool;
    BridgeSlaPolicy sla_policy;
    BridgePolicyTable policy_table;
    std::uint32_t receive_priority{0U};
    std::uint32_t trace_sample_rate_percent{10U};
    std::uint64_t trace_sample_interval_ms{2000U};
    std::uint64_t sla_violation_sample_interval_ms{5000U};
    std::uint64_t diagnostics_report_interval_ms{1000U};
    bool enable_diagnostics_reporting{false};
    bool enable_config_canary{false};
    std::uint32_t config_canary_percent{100U};
    std::string config_canary_topic_prefix;
    std::int32_t config_canary_channel{-1};
    std::string protocol_canary_topic_prefix;
    std::uint32_t protocol_canary_percent{100U};
    std::vector<std::string> idempotency_topics;
    std::uint32_t idempotency_window_size{128U};
    std::string diagnostics_snapshot_path{"build/diagnostics_snapshots.jsonl"};
    std::uint32_t diagnostics_snapshot_limit{100U};
    bool enable_config_risk_guard{false};
    bool auto_rollback_on_high_risk{true};
    std::uint32_t high_risk_block_threshold{3U};
    MessageProtocolMode protocol_mode{MessageProtocolMode::kLegacyPipe};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_BRIDGE_CONFIG_HPP
