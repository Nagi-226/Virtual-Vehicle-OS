#ifndef VR_INTERCONNECT_POLICY_CACHE_HPP
#define VR_INTERCONNECT_POLICY_CACHE_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "interconnect/bridge_policy.hpp"
#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

// 策略解析缓存的值。
//
// 头文件拓扑说明：这些类型原先定义在 InterconnectBridge 类内部
// （interconnect_bridge.hpp:238 起），而同一类的成员函数声明（:145-146）
// 已经把 PolicyCacheEntry 用作返回类型。C++ 的类内名字查找对**成员函数声明**
// 的返回类型不生效（只对函数体/默认实参生效），所以那是非法前向引用
// （诊断见 A1.3）。下沉到命名空间作用域后，声明处即可见。
struct PolicyCacheEntry {
    BridgeSlaPolicy policy{};
    bool override_applied{false};
    bool conflict_detected{false};
};

// 策略解析缓存的键：topic + channel + qos 三元组。
struct PolicyCacheKey {
    std::string topic;
    ChannelType channel{ChannelType::kEvent};
    DeliveryQoS qos{DeliveryQoS::kBestEffort};

    bool operator==(const PolicyCacheKey& other) const {
        return topic == other.topic && channel == other.channel && qos == other.qos;
    }
};

struct PolicyCacheKeyHasher {
    std::size_t operator()(const PolicyCacheKey& key) const noexcept {
        const std::size_t h1 = std::hash<std::string>{}(key.topic);
        const std::size_t h2 = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.channel));
        const std::size_t h3 = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.qos));
        return h1 ^ (h2 << 1U) ^ (h3 << 2U);
    }
};

// LRU 链表节点：键与值成对存放，便于淘汰时反查索引。
struct PolicyCacheItem {
    PolicyCacheKey key;
    PolicyCacheEntry entry;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_POLICY_CACHE_HPP
