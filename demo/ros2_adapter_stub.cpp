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
