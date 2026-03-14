#include "interconnect/posix_mq_transport.hpp"

namespace vr {
namespace interconnect {

const char* PosixMqTransport::Name() const noexcept {
    return "posix_mq";
}

TransportCapabilities PosixMqTransport::Caps() const noexcept {
    TransportCapabilities caps;
    caps.supports_priority = true;
    caps.supports_discard_oldest = true;
    caps.supports_unlink = true;
    return caps;
}

vr::core::ErrorCode PosixMqTransport::Create(const TransportEndpointConfig& config) noexcept {
    vr::ipc::QueueConfig queue_config;
    queue_config.name = config.name;
    queue_config.max_messages = config.max_messages;
    queue_config.message_size = config.message_size;
    return queue_.Create(queue_config);
}

vr::core::ErrorCode PosixMqTransport::SendWithTimeout(const std::string& message,
                                                      const std::uint32_t priority,
                                                      const std::int64_t timeout_ms) noexcept {
    return queue_.SendWithTimeout(message, priority, timeout_ms);
}

vr::core::ErrorCode PosixMqTransport::ReceiveWithTimeout(std::string* const message,
                                                         std::uint32_t* const priority,
                                                         const std::int64_t timeout_ms) noexcept {
    return queue_.ReceiveWithTimeout(message, priority, timeout_ms);
}

vr::core::ErrorCode PosixMqTransport::DiscardOldest() noexcept {
    std::string ignored;
    std::uint32_t priority = 0U;
    const vr::core::ErrorCode ec = queue_.ReceiveNonBlocking(&ignored, &priority);
    if (ec == vr::core::ErrorCode::kWouldBlock) {
        return vr::core::ErrorCode::kOk;
    }
    return ec;
}

void PosixMqTransport::Close() noexcept {
    queue_.Close();
}

void PosixMqTransport::Unlink() noexcept {
    queue_.Unlink();
}

}  // namespace interconnect
}  // namespace vr
