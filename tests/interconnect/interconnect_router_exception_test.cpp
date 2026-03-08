#include <iostream>
#include <stdexcept>
#include <string>

#include "interconnect/message_envelope.hpp"
#include "interconnect/message_router.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

bool TestRouterExceptionIsolation() {
    vr::interconnect::MessageRouter router;

    const bool register_ok = router.Register("topic.throw", [](const vr::interconnect::MessageEnvelope&) {
        throw std::runtime_error("handler failed");
    });
    if (!ExpectTrue(register_ok, "router register should succeed")) {
        return false;
    }

    vr::interconnect::MessageEnvelope envelope;
    envelope.topic = "topic.throw";

    const bool route_ok = router.Route(envelope);
    return ExpectTrue(!route_ok, "router should return false when handler throws");
}

}  // namespace

int main() {
    const bool ok = TestRouterExceptionIsolation();
    if (!ok) {
        std::cerr << "interconnect router exception test failed." << std::endl;
        return 1;
    }

    std::cout << "interconnect router exception test passed." << std::endl;
    return 0;
}

