#ifndef VR_INTERCONNECT_TRANSPORT_HPP
#define VR_INTERCONNECT_TRANSPORT_HPP

#include <cstdint>
#include <string>

#include "core/error_code.hpp"

namespace vr {
namespace interconnect {

struct TransportEndpointConfig {
    std::string name;
    long max_messages{32};
    long message_size{1024};
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual vr::core::ErrorCode Create(const TransportEndpointConfig& config) noexcept = 0;
    virtual vr::core::ErrorCode SendWithTimeout(const std::string& message,
                                                std::uint32_t priority,
                                                std::int64_t timeout_ms) noexcept = 0;
    virtual vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                                   std::int64_t timeout_ms) noexcept = 0;
    virtual void Close() noexcept = 0;
    virtual void Unlink() noexcept = 0;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_TRANSPORT_HPP
