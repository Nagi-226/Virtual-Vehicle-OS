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

    // Route() 返回 RouteResult（kOk=0/kNoHandler/kHandlerError）。
    // 原写法把枚举直接赋给 bool，"处理函数抛异常时不应返回成功"这条断言的
    // 语义保持不变：RouteResult 非 kOk 即为失败。
    const vr::interconnect::RouteResult route_result = router.Route(envelope);
    return ExpectTrue(route_result != vr::interconnect::RouteResult::kOk,
                      "router should return false when handler throws");
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

