#include "core/retry_policy.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace vr {
namespace core {

ErrorCode RetryPolicy::RetryOnWouldBlock(const std::function<ErrorCode()>& operation,
                                         const RetryOptions& options) noexcept {
    if (!operation || options.max_retries < 0 || options.initial_backoff_ms <= 0 ||
        options.max_backoff_ms < options.initial_backoff_ms) {
        return ErrorCode::kInvalidParam;
    }

    std::int64_t backoff_ms = options.initial_backoff_ms;

    for (std::int32_t attempt = 0; attempt <= options.max_retries; ++attempt) {
        const ErrorCode ec = operation();
        if (ec == ErrorCode::kOk) {
            return ErrorCode::kOk;
        }

        if (ec != ErrorCode::kWouldBlock) {
            return ec;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min<std::int64_t>(backoff_ms * 2, options.max_backoff_ms);
    }

    return ErrorCode::kTimeout;
}

}  // namespace core
}  // namespace vr


