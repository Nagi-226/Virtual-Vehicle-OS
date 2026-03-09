#ifndef VR_INTERCONNECT_POSIX_MQ_TRANSPORT_HPP
#define VR_INTERCONNECT_POSIX_MQ_TRANSPORT_HPP

#include "interconnect/transport.hpp"
#include "ipc/posix_message_queue.hpp"

namespace vr {
namespace interconnect {

class PosixMqTransport final : public ITransport {
public:
    vr::core::ErrorCode Create(const TransportEndpointConfig& config) noexcept override;
    vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                        std::int64_t timeout_ms) noexcept override;
    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           std::int64_t timeout_ms) noexcept override;
    vr::core::ErrorCode DiscardOldest() noexcept override;
    void Close() noexcept override;
    void Unlink() noexcept override;

private:
    vr::ipc::PosixMessageQueue queue_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_POSIX_MQ_TRANSPORT_HPP
