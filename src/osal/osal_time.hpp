#ifndef VR_OSAL_TIME_HPP
#define VR_OSAL_TIME_HPP

#include <chrono>
#include <cstdint>
#include <thread>

namespace vr {
namespace osal {

inline std::uint64_t NowMs() noexcept {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now());
    return static_cast<std::uint64_t>(now.time_since_epoch().count());
}

inline void SleepForMs(std::uint64_t duration_ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
}

}  // namespace osal
}  // namespace vr

#endif  // VR_OSAL_TIME_HPP
