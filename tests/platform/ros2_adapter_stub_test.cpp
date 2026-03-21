#include <iostream>
#include <string>

namespace vr {
namespace platform {

struct Ros2QosProfileStub {
    bool reliable{false};
    std::uint32_t depth{10U};
};

class Ros2AdapterStub {
public:
    bool ConfigureTopic(const std::string& topic, const Ros2QosProfileStub& qos) {
        topic_ = topic;
        qos_ = qos;
        return !topic_.empty();
    }

    std::string MapToEnvelopeTopic(const std::string& ros2_topic) const {
        return "ros2://" + ros2_topic;
    }

private:
    std::string topic_;
    Ros2QosProfileStub qos_{};
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

bool TestRos2StubMapping() {
    vr::platform::Ros2AdapterStub adapter;
    vr::platform::Ros2QosProfileStub qos;
    qos.reliable = true;
    qos.depth = 20U;

    if (!ExpectTrue(adapter.ConfigureTopic("/robot/cmd", qos), "configure failed")) {
        return false;
    }

    const std::string mapped = adapter.MapToEnvelopeTopic("/robot/cmd");
    return ExpectTrue(mapped == "ros2:///robot/cmd", "mapping mismatch");
}

}  // namespace

int main() {
    if (!TestRos2StubMapping()) {
        std::cerr << "ros2 adapter stub test failed." << std::endl;
        return 1;
    }

    std::cout << "ros2 adapter stub test passed." << std::endl;
    return 0;
}
