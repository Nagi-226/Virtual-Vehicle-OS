#ifndef VR_OSAL_MUTEX_HPP
#define VR_OSAL_MUTEX_HPP

#include <mutex>

namespace vr {
namespace osal {

using Mutex = std::mutex;
using LockGuard = std::lock_guard<Mutex>;
using UniqueLock = std::unique_lock<Mutex>;

}  // namespace osal
}  // namespace vr

#endif  // VR_OSAL_MUTEX_HPP
