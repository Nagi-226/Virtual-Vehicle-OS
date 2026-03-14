#ifndef VR_OSAL_EVENT_HPP
#define VR_OSAL_EVENT_HPP

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace vr {
namespace osal {

class Event {
public:
    void NotifyOne() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_one();
    }

    void NotifyAll() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signaled_ = true;
        }
        cv_.notify_all();
    }

    void Reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        signaled_ = false;
    }

    void Wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return signaled_; });
    }

    bool WaitFor(std::int64_t timeout_ms) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this]() { return signaled_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_{false};
};

}  // namespace osal
}  // namespace vr

#endif  // VR_OSAL_EVENT_HPP
