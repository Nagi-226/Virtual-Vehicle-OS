#ifndef VR_INTERCONNECT_TCP_TRANSPORT_HPP
#define VR_INTERCONNECT_TCP_TRANSPORT_HPP

#include <cstdint>
#include <string>

#include "interconnect/transport.hpp"

namespace vr {
namespace interconnect {

class TcpTransport final : public ITransport {
public:
    const char* Name() const noexcept override;
    TransportCapabilities Caps() const noexcept override;

    vr::core::ErrorCode Create(const TransportEndpointConfig& config) noexcept override;
    vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                        std::int64_t timeout_ms) noexcept override;
    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           std::int64_t timeout_ms) noexcept override;
    vr::core::ErrorCode DiscardOldest() noexcept override;

    void Close() noexcept override;
    void Unlink() noexcept override;

private:
    vr::core::ErrorCode Connect(const std::string& host, std::uint16_t port,
                                std::int64_t timeout_ms) noexcept;
    void CloseSocket() noexcept;

    int socket_fd_{-1};
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_TCP_TRANSPORT_HPP
