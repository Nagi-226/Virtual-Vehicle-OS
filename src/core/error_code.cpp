#include "core/error_code.hpp"

namespace vr {
namespace core {

const char* ToString(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::kOk:
            return "OK";
        case ErrorCode::kInvalidParam:
            return "INVALID_PARAM";
        case ErrorCode::kTimeout:
            return "TIMEOUT";
        case ErrorCode::kQueueCreateFailed:
            return "QUEUE_CREATE_FAILED";
        case ErrorCode::kQueueSendFailed:
            return "QUEUE_SEND_FAILED";
        case ErrorCode::kQueueReceiveFailed:
            return "QUEUE_RECEIVE_FAILED";
        case ErrorCode::kThreadStartFailed:
            return "THREAD_START_FAILED";
        case ErrorCode::kThreadPrioritySetFailed:
            return "THREAD_PRIORITY_SET_FAILED";
        case ErrorCode::kProcessForkFailed:
            return "PROCESS_FORK_FAILED";
        case ErrorCode::kProcessWaitFailed:
            return "PROCESS_WAIT_FAILED";
        default:
            return "UNKNOWN";
    }
}

}  // namespace core
}  // namespace vr

