#ifndef VR_INTERCONNECT_MESSAGE_ROUTER_HPP
#define VR_INTERCONNECT_MESSAGE_ROUTER_HPP

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

#include "interconnect/message_envelope.hpp"

namespace vr {
namespace interconnect {

using MessageHandler = std::function<void(const MessageEnvelope& envelope)>;

enum class RouteResult : std::uint8_t {
    kOk = 0,
    kNoHandler = 1,
    kHandlerError = 2
};

class MessageRouter {
public:
    bool Register(const std::string& topic, MessageHandler handler);
    RouteResult Route(const MessageEnvelope& envelope) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, MessageHandler> handlers_;
};

}  // namespace interconnect
}  // namespace vr

#endif  // VR_INTERCONNECT_MESSAGE_ROUTER_HPP
