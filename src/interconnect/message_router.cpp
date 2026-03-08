#include "interconnect/message_router.hpp"

#include <utility>

namespace vr {
namespace interconnect {

bool MessageRouter::Register(const std::string& topic, MessageHandler handler) {
    if (topic.empty() || !handler) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    handlers_[topic] = std::move(handler);
    return true;
}

bool MessageRouter::Route(const MessageEnvelope& envelope) const {
    MessageHandler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = handlers_.find(envelope.topic);
        if (it == handlers_.end()) {
            return false;
        }
        handler = it->second;
    }

    try {
        handler(envelope);
    } catch (...) {
        return false;
    }

    return true;
}

}  // namespace interconnect
}  // namespace vr
