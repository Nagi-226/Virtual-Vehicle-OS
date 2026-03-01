#include "core/process_guardian.hpp"

#include <sys/wait.h>
#include <unistd.h>

namespace vr {
namespace core {

ErrorCode ProcessGuardian::RunWithRestart(const std::function<int()>& service_entry,
                                          const int max_restart_times,
                                          int* const final_status) noexcept {
    if (!service_entry || final_status == nullptr || max_restart_times < 0) {
        return ErrorCode::kInvalidParam;
    }

    int restart_count = 0;

    while (restart_count <= max_restart_times) {
        const pid_t pid = fork();
        if (pid < 0) {
            return ErrorCode::kProcessForkFailed;
        }

        if (pid == 0) {
            const int code = service_entry();
            _exit(code);
        }

        int status = 0;
        const pid_t wait_rc = waitpid(pid, &status, 0);
        if (wait_rc < 0) {
            return ErrorCode::kProcessWaitFailed;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            *final_status = 0;
            return ErrorCode::kOk;
        }

        ++restart_count;
    }

    *final_status = -1;
    return ErrorCode::kProcessWaitFailed;
}

}  // namespace core
}  // namespace vr

