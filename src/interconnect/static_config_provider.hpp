#ifndef VR_INTERCONNECT_STATIC_CONFIG_PROVIDER_HPP
#define VR_INTERCONNECT_STATIC_CONFIG_PROVIDER_HPP

#include <cstdint>
#include <string>
#include <utility>

#include "interconnect/config_provider.hpp"
#include "interconnect/interconnect_bridge.hpp"

namespace vr {
namespace interconnect {

class StaticConfigProvider final : public IConfigProvider {
public:
    StaticConfigProvider(BridgeConfig config, std::string source)
        : config_(std::move(config)), source_(std::move(source)) {}

    vr::core::ErrorCode LoadBridgeConfig(BridgeConfig* out_config,
                                         std::string* out_source) noexcept override {
        if (out_config == nullptr || out_source == nullptr) {
            return vr::core::ErrorCode::kInvalidParam;
        }

        *out_config = config_;
        *out_source = source_;
        return vr::core::ErrorCode::kOk;
    }

    std::uint64_t GetVersion() const noexcept override { return version_; }

    vr::core::ErrorCode Reload() noexcept override {
        return vr::core::ErrorCode::kOk;
    }

    void UpdateConfigForTest(BridgeConfig config, std::string source) {
        config_ = std::move(config);
        source_ = std::move(source);
        ++version_;
    }

private:
    BridgeConfig config_;
    std::string source_;
    std::uint64_t version_{1U};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_STATIC_CONFIG_PROVIDER_HPP
