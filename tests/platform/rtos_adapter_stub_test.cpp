#include <iostream>
#include <string>

#include "core/error_code.hpp"

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

namespace {

bool ExpectTrue(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAILED] " << msg << std::endl;
        return false;
    }
    return true;
}

bool TestRtosStubRoundTrip() {
    vr::platform::RtosQueueStub queue;
    if (!ExpectTrue(queue.Init(8U, 256U), "init failed")) {
        return false;
    }

    if (!ExpectTrue(queue.Send("rtos_payload"), "send failed")) {
        return false;
    }

    std::string out;
    if (!ExpectTrue(queue.Receive(&out), "receive failed")) {
        return false;
    }

    return ExpectTrue(out == "rtos_payload", "payload mismatch");
}

}  // namespace

int main() {
    if (!TestRtosStubRoundTrip()) {
        std::cerr << "rtos adapter stub test failed." << std::endl;
        return 1;
    }

    std::cout << "rtos adapter stub test passed." << std::endl;
    return 0;
}
