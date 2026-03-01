#ifndef VR_CORE_PROCESS_GUARDIAN_HPP
#define VR_CORE_PROCESS_GUARDIAN_HPP

#include <functional>
#include <sys/types.h>

#include "core/error_code.hpp"

namespace vr {
namespace core {

class ProcessGuardian {
public:
    ProcessGuardian() = default;
    ~ProcessGuardian() = default;

    ErrorCode RunWithRestart(const std::function<int()>& service_entry,
                             int max_restart_times,
                             int* final_status) noexcept;
};

}  // namespace core
}  // namespace vr

#endif  // VR_CORE_PROCESS_GUARDIAN_HPP

