#include <cstdint>
#include <string>

namespace vr {
namespace platform {

struct RtosTaskConfig {
    std::uint32_t priority{5U};
    std::uint32_t stack_size{2048U};
};

class RtosQueueStub {
public:
    bool Init(std::uint32_t depth, std::uint32_t bytes) {
        (void)depth;
        (void)bytes;
        initialized_ = true;
        return true;
    }

    bool Send(const std::string& payload) {
        if (!initialized_) {
            return false;
        }
        last_payload_ = payload;
        return true;
    }

    bool Receive(std::string* out) {
        if (!initialized_ || out == nullptr) {
            return false;
        }
        *out = last_payload_;
        return true;
    }

private:
    bool initialized_{false};
    std::string last_payload_;
};

}  // namespace platform
}  // namespace vr
