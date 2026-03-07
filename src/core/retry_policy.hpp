#ifndef VR_CORE_RETRY_POLICY_HPP
#define VR_CORE_RETRY_POLICY_HPP

#include <cstdint>
#include <functional>

#include "core/error_code.hpp"

namespace vr {
namespace core {

struct RetryOptions {
    std::int32_t max_retries{5};
    std::int64_t initial_backoff_ms{10};
    std::int64_t max_backoff_ms{200};
};

class RetryPolicy {
public:
    static ErrorCode RetryOnWouldBlock(const std::function<ErrorCode()>& operation,
                                       const RetryOptions& options) noexcept;
};

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_RETRY_POLICY_HPP


