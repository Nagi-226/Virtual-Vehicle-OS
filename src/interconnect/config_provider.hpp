#ifndef VR_INTERCONNECT_CONFIG_PROVIDER_HPP
#define VR_INTERCONNECT_CONFIG_PROVIDER_HPP

#include <string>

#include "core/error_code.hpp"

namespace vr {
namespace interconnect {

struct BridgeConfig;

class IConfigProvider {
public:
    virtual ~IConfigProvider() = default;
    virtual vr::core::ErrorCode LoadBridgeConfig(BridgeConfig* out_config,
                                                  std::string* out_source) noexcept = 0;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_CONFIG_PROVIDER_HPP
