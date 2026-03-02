#include <cstdint>
#include <iostream>
#include <string>

#include "core/error_code.hpp"
#include "ipc/posix_message_queue.hpp"

namespace {

using vr::core::ErrorCode;
using vr::ipc::PosixMessageQueue;
using vr::ipc::QueueConfig;

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestCreateWithInvalidQueueName() {
    PosixMessageQueue queue;
    QueueConfig config;
    config.name = "invalid_name";

    const ErrorCode ec = queue.Create(config);
    return ExpectTrue(ec == ErrorCode::kInvalidParam,
                      "Create should fail when queue name does not start with '/'");
}

bool TestCreateSendReceiveAndCleanup() {
    PosixMessageQueue queue;
    QueueConfig config;
    config.name = "/vr_ipc_test_queue_a";
    config.max_messages = 8;
    config.message_size = 128;

    queue.Unlink();
    if (!ExpectTrue(queue.Create(config) == ErrorCode::kOk, "Create queue failed")) {
        return false;
    }

    const std::string payload = "ipc_test_payload";
    if (!ExpectTrue(queue.Send(payload, 3U) == ErrorCode::kOk, "Send message failed")) {
        queue.Close();
        queue.Unlink();
        return false;
    }

    std::string recv_message;
    std::uint32_t recv_priority = 0U;
    const ErrorCode recv_ec = queue.Receive(&recv_message, &recv_priority);

    queue.Close();
    queue.Unlink();

    return ExpectTrue(recv_ec == ErrorCode::kOk, "Receive message failed") &&
           ExpectTrue(recv_message == payload, "Received payload mismatch") &&
           ExpectTrue(recv_priority == 3U, "Received priority mismatch");
}

bool TestSendOversizedMessage() {
    PosixMessageQueue queue;
    QueueConfig config;
    config.name = "/vr_ipc_test_queue_b";
    config.max_messages = 8;
    config.message_size = 16;

    queue.Unlink();
    if (!ExpectTrue(queue.Create(config) == ErrorCode::kOk, "Create queue failed for oversize test")) {
        return false;
    }

    const std::string oversized(32U, 'X');
    const ErrorCode ec = queue.Send(oversized, 1U);

    queue.Close();
    queue.Unlink();

    return ExpectTrue(ec == ErrorCode::kInvalidParam,
                      "Send should fail when payload exceeds queue message size");
}

bool TestReceiveWithNullOutput() {
    PosixMessageQueue queue;
    QueueConfig config;
    config.name = "/vr_ipc_test_queue_c";

    queue.Unlink();
    if (!ExpectTrue(queue.Create(config) == ErrorCode::kOk, "Create queue failed for null receive test")) {
        return false;
    }

    std::uint32_t priority = 0U;
    const ErrorCode ec = queue.Receive(nullptr, &priority);

    queue.Close();
    queue.Unlink();

    return ExpectTrue(ec == ErrorCode::kInvalidParam, "Receive should fail when message pointer is null");
}

}  // namespace

int main() {
    const bool ok = TestCreateWithInvalidQueueName() && TestCreateSendReceiveAndCleanup() &&
                    TestSendOversizedMessage() && TestReceiveWithNullOutput();

    if (!ok) {
        std::cerr << "POSIX message queue tests failed." << std::endl;
        return 1;
    }

    std::cout << "POSIX message queue tests passed." << std::endl;
    return 0;
}

