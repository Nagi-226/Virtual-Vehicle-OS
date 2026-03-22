#include <iostream>
#include <string>

namespace vr {
namespace platform {

struct Ros2QosProfileSample {
    bool reliable{true};
    std::uint32_t depth{10U};
};

class Ros2AdapterSample {
public:
    bool ConfigureTopic(const std::string& topic, const Ros2QosProfileSample& qos) {
        topic_ = topic;
        qos_ = qos;
        return !topic_.empty();
    }

    std::string MapToEnvelopeTopic(const std::string& ros2_topic) const {
        return "ros2://" + ros2_topic;
    }

private:
    std::string topic_;
    Ros2QosProfileSample qos_{};
};

}  // namespace platform
}  // namespace vr

int main() {
    vr::platform::Ros2AdapterSample adapter;
    vr::platform::Ros2QosProfileSample qos;
    if (!adapter.ConfigureTopic("/robot/cmd", qos)) {
        return 1;
    }

    const std::string mapped = adapter.MapToEnvelopeTopic("/robot/cmd");
    std::cout << "ros2 adapter sample ok: " << mapped << std::endl;
    return mapped == "ros2:///robot/cmd" ? 0 : 1;
}
