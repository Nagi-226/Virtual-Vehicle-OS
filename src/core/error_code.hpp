#ifndef VR_CORE_ERROR_CODE_HPP
#define VR_CORE_ERROR_CODE_HPP

#include <cstdint>
#include <string>

namespace vr {
namespace core {

enum class ErrorCode : std::int32_t {
    kOk = 0,
    kInvalidParam = 1000,
    kTimeout = 1001,
    kQueueCreateFailed = 2000,
    kQueueSendFailed = 2001,
    kQueueReceiveFailed = 2002,
    kThreadStartFailed = 3000,
    kThreadPrioritySetFailed = 3001,
    kProcessForkFailed = 4000,
    kProcessWaitFailed = 4001,
    kUnknown = 9000
};

const char* ToString(ErrorCode code) noexcept;

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_ERROR_CODE_HPP

