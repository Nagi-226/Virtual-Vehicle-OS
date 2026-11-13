#ifndef VR_CORE_ERROR_CODE_HPP
#define VR_CORE_ERROR_CODE_HPP

#include <cstdint>

namespace vr {
namespace core {

enum class ErrorDomain : std::uint16_t {
    kCommon = 0x00,
    kCore = 0x01,
    kIpc = 0x02,
    kLog = 0x03,
    kDemo = 0x04,
    kInterconnect = 0x05
};

constexpr std::int32_t MakeErrorCode(const ErrorDomain domain, const std::uint16_t detail) noexcept {
    return static_cast<std::int32_t>((static_cast<std::uint32_t>(domain) << 16U) | detail);
}

enum class ErrorCode : std::int32_t {
    kOk = 0,

    kInvalidParam = MakeErrorCode(ErrorDomain::kCommon, 0x0001),
    kTimeout = MakeErrorCode(ErrorDomain::kCommon, 0x0002),
    kWouldBlock = MakeErrorCode(ErrorDomain::kCommon, 0x0003),
    // 报文 TTL 耗尽（已过期）。当前实现由接收侧 ProcessInbound() 的 IsExpired()
    // 判定并丢弃，只累加 expired_drop_count 指标；发布侧 Publish() 尚未做前置校验，
    // 因此这个码目前是"可表达但未从发布路径返回"的状态。
    kExpired = MakeErrorCode(ErrorDomain::kCommon, 0x0004),

    kThreadStartFailed = MakeErrorCode(ErrorDomain::kCore, 0x0001),
    kThreadPrioritySetFailed = MakeErrorCode(ErrorDomain::kCore, 0x0002),
    kThreadQueueFull = MakeErrorCode(ErrorDomain::kCore, 0x0003),
    kThreadTaskRejected = MakeErrorCode(ErrorDomain::kCore, 0x0004),
    kProcessForkFailed = MakeErrorCode(ErrorDomain::kCore, 0x0005),
    kProcessWaitFailed = MakeErrorCode(ErrorDomain::kCore, 0x0006),

    kQueueCreateFailed = MakeErrorCode(ErrorDomain::kIpc, 0x0001),
    kQueueSendFailed = MakeErrorCode(ErrorDomain::kIpc, 0x0002),
    kQueueReceiveFailed = MakeErrorCode(ErrorDomain::kIpc, 0x0003),

    kLogWriteFailed = MakeErrorCode(ErrorDomain::kLog, 0x0001),

    kDemoServiceFailed = MakeErrorCode(ErrorDomain::kDemo, 0x0001),

    kInterconnectInvalidEnvelope = MakeErrorCode(ErrorDomain::kInterconnect, 0x0001),
    kInterconnectRouteMiss = MakeErrorCode(ErrorDomain::kInterconnect, 0x0002),
    kInterconnectRouteHandlerError = MakeErrorCode(ErrorDomain::kInterconnect, 0x0003),

    kUnknown = MakeErrorCode(ErrorDomain::kCommon, 0xFFFF)
};

const char* ToString(ErrorCode code) noexcept;
const char* ToDomainString(ErrorCode code) noexcept;
std::uint16_t GetDetailCode(ErrorCode code) noexcept;

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_ERROR_CODE_HPP
