#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>

#include "core/error_code.hpp"
#include "core/retry_policy.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestRetrySucceedsAfterWouldBlock() {
    std::atomic<std::int32_t> call_count{0};

    const vr::core::RetryOptions options{5, 1, 8};
    const vr::core::ErrorCode ec = vr::core::RetryPolicy::RetryOnWouldBlock(
        [&call_count]() {
            const std::int32_t current = ++call_count;
            if (current < 3) {
                return vr::core::ErrorCode::kWouldBlock;
            }
            return vr::core::ErrorCode::kOk;
        },
        options);

    return ExpectTrue(ec == vr::core::ErrorCode::kOk,
                      "Retry should succeed when operation eventually returns OK") &&
           ExpectTrue(call_count.load() == 3, "Call count should be 3 for this scenario");
}

bool TestRetryTimeoutAfterMaxRetries() {
    std::atomic<std::int32_t> call_count{0};

    const vr::core::RetryOptions options{2, 1, 8};
    const vr::core::ErrorCode ec = vr::core::RetryPolicy::RetryOnWouldBlock(
        [&call_count]() {
            ++call_count;
            return vr::core::ErrorCode::kWouldBlock;
        },
        options);

    return ExpectTrue(ec == vr::core::ErrorCode::kTimeout,
                      "Retry should return TIMEOUT when WOULD_BLOCK persists") &&
           ExpectTrue(call_count.load() == 3,
                      "Call count should be max_retries + 1 when operation keeps WOULD_BLOCK");
}

bool TestRetryStopsOnNonRetryableError() {
    std::atomic<std::int32_t> call_count{0};

    const vr::core::RetryOptions options{5, 1, 8};
    const vr::core::ErrorCode ec = vr::core::RetryPolicy::RetryOnWouldBlock(
        [&call_count]() {
            ++call_count;
            return vr::core::ErrorCode::kQueueSendFailed;
        },
        options);

    return ExpectTrue(ec == vr::core::ErrorCode::kQueueSendFailed,
                      "Retry should stop immediately on non-WOULD_BLOCK error") &&
           ExpectTrue(call_count.load() == 1, "Non-retryable error should not loop");
}

bool TestInvalidOptions() {
    const vr::core::RetryOptions bad_retries{-1, 1, 8};
    const vr::core::RetryOptions bad_initial_backoff{1, 0, 8};
    const vr::core::RetryOptions bad_max_backoff{1, 10, 1};

    const auto always_ok = []() { return vr::core::ErrorCode::kOk; };

    const vr::core::ErrorCode ec1 =
        vr::core::RetryPolicy::RetryOnWouldBlock(always_ok, bad_retries);
    const vr::core::ErrorCode ec2 =
        vr::core::RetryPolicy::RetryOnWouldBlock(always_ok, bad_initial_backoff);
    const vr::core::ErrorCode ec3 =
        vr::core::RetryPolicy::RetryOnWouldBlock(always_ok, bad_max_backoff);

    return ExpectTrue(ec1 == vr::core::ErrorCode::kInvalidParam,
                      "Negative max_retries should return INVALID_PARAM") &&
           ExpectTrue(ec2 == vr::core::ErrorCode::kInvalidParam,
                      "Non-positive initial_backoff should return INVALID_PARAM") &&
           ExpectTrue(ec3 == vr::core::ErrorCode::kInvalidParam,
                      "max_backoff less than initial_backoff should return INVALID_PARAM");
}

bool TestBackoffUpperBoundEffect() {
    std::atomic<std::int32_t> call_count{0};

    const vr::core::RetryOptions options{4, 5, 10};
    const auto start = std::chrono::steady_clock::now();
    const vr::core::ErrorCode ec = vr::core::RetryPolicy::RetryOnWouldBlock(
        [&call_count]() {
            ++call_count;
            return vr::core::ErrorCode::kWouldBlock;
        },
        options);
    const auto end = std::chrono::steady_clock::now();

    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return ExpectTrue(ec == vr::core::ErrorCode::kTimeout,
                      "Backoff upper bound test should end with TIMEOUT") &&
           ExpectTrue(call_count.load() == 5,
                      "Call count should be max_retries + 1 in upper bound test") &&
           ExpectTrue(elapsed_ms >= 35,
                      "Elapsed time should include capped backoff waits (>=35ms expected)") &&
           ExpectTrue(elapsed_ms < 120,
                      "Elapsed time should stay bounded when max_backoff is capped");
}

}  // namespace

int main() {
    const bool ok = TestRetrySucceedsAfterWouldBlock() && TestRetryTimeoutAfterMaxRetries() &&
                    TestRetryStopsOnNonRetryableError() && TestInvalidOptions() &&
                    TestBackoffUpperBoundEffect();

    if (!ok) {
        std::cerr << "RetryPolicy tests failed." << std::endl;
        return 1;
    }

    std::cout << "RetryPolicy tests passed." << std::endl;
    return 0;
}
