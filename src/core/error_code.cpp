#include "core/error_code.hpp"

namespace vr {
namespace core {

namespace {

ErrorDomain GetDomain(const ErrorCode code) noexcept {
    const auto raw = static_cast<std::uint32_t>(code);
    return static_cast<ErrorDomain>((raw >> 16U) & 0xFFFFU);
}

}  // namespace

const char* ToString(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::kOk:
            return "OK";
        case ErrorCode::kInvalidParam:
            return "INVALID_PARAM";
        case ErrorCode::kTimeout:
            return "TIMEOUT";
        case ErrorCode::kThreadStartFailed:
            return "THREAD_START_FAILED";
        case ErrorCode::kThreadPrioritySetFailed:
            return "THREAD_PRIORITY_SET_FAILED";
        case ErrorCode::kThreadQueueFull:
            return "THREAD_QUEUE_FULL";
        case ErrorCode::kThreadTaskRejected:
            return "THREAD_TASK_REJECTED";
        case ErrorCode::kProcessForkFailed:
            return "PROCESS_FORK_FAILED";
        case ErrorCode::kProcessWaitFailed:
            return "PROCESS_WAIT_FAILED";
        case ErrorCode::kQueueCreateFailed:
            return "QUEUE_CREATE_FAILED";
        case ErrorCode::kQueueSendFailed:
            return "QUEUE_SEND_FAILED";
        case ErrorCode::kQueueReceiveFailed:
            return "QUEUE_RECEIVE_FAILED";
        case ErrorCode::kLogWriteFailed:
            return "LOG_WRITE_FAILED";
        case ErrorCode::kDemoServiceFailed:
            return "DEMO_SERVICE_FAILED";
        default:
            return "UNKNOWN";
    }
}

const char* ToDomainString(const ErrorCode code) noexcept {
    switch (GetDomain(code)) {
        case ErrorDomain::kCommon:
            return "COMMON";
        case ErrorDomain::kCore:
            return "CORE";
        case ErrorDomain::kIpc:
            return "IPC";
        case ErrorDomain::kLog:
            return "LOG";
        case ErrorDomain::kDemo:
            return "DEMO";
        default:
            return "UNKNOWN";
    }
}

std::uint16_t GetDetailCode(const ErrorCode code) noexcept {
    const auto raw = static_cast<std::uint32_t>(code);
    return static_cast<std::uint16_t>(raw & 0xFFFFU);
}

}  // namespace core
}  // namespace vr
