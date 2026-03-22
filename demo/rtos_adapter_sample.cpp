#include <cstdint>
#include <iostream>
#include <string>

namespace vr {
namespace platform {

class RtosQueueSample {
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
        payload_ = payload;
        return true;
    }

    bool Receive(std::string* out) {
        if (!initialized_ || out == nullptr) {
            return false;
        }
        *out = payload_;
        return true;
    }

private:
    bool initialized_{false};
    std::string payload_;
};

}  // namespace platform
}  // namespace vr

int main() {
    vr::platform::RtosQueueSample queue;
    if (!queue.Init(8U, 256U)) {
        return 1;
    }

    if (!queue.Send("rtos_sample_payload")) {
        return 1;
    }

    std::string out;
    if (!queue.Receive(&out)) {
        return 1;
    }

    std::cout << "rtos adapter sample ok: " << out << std::endl;
    return out == "rtos_sample_payload" ? 0 : 1;
}
