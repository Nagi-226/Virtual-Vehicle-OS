#ifndef VR_IPC_POSIX_MESSAGE_QUEUE_HPP
#define VR_IPC_POSIX_MESSAGE_QUEUE_HPP

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/error_code.hpp"

namespace vr {
namespace ipc {

struct QueueConfig {
    std::string name{"/vr_demo_queue"};
    long max_messages{10};
    long message_size{256};
};

class PosixMessageQueue {
public:
    PosixMessageQueue() = default;
    ~PosixMessageQueue();

    vr::core::ErrorCode Create(const QueueConfig& config) noexcept;
    vr::core::ErrorCode Open(const QueueConfig& config) noexcept;
    vr::core::ErrorCode Send(const std::string& message, std::uint32_t priority) noexcept;
    vr::core::ErrorCode SendWithTimeout(const std::string& message, std::uint32_t priority,
                                        std::int64_t timeout_ms) noexcept;
    vr::core::ErrorCode Receive(std::string* message, std::uint32_t* priority) noexcept;
    vr::core::ErrorCode ReceiveWithTimeout(std::string* message, std::uint32_t* priority,
                                           std::int64_t timeout_ms) noexcept;
    void Close() noexcept;
    void Unlink() noexcept;

private:
    std::string queue_name_;
    int mqd_{-1};
    long message_size_{256};
};

}  // namespace ipc
}  // namespace vr

#endif  // VR_IPC_POSIX_MESSAGE_QUEUE_HPP
