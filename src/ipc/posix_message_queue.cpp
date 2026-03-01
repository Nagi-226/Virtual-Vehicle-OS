#include "ipc/posix_message_queue.hpp"

#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <vector>

namespace vr {
namespace ipc {

PosixMessageQueue::~PosixMessageQueue() {
    Close();
}

vr::core::ErrorCode PosixMessageQueue::Create(const QueueConfig& config) noexcept {
    if (config.name.empty() || config.name[0] != '/') {
        return vr::core::ErrorCode::kInvalidParam;
    }

    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = config.max_messages;
    attr.mq_msgsize = config.message_size;
    attr.mq_curmsgs = 0;

    queue_name_ = config.name;
    message_size_ = config.message_size;

    mqd_ = static_cast<int>(mq_open(queue_name_.c_str(), O_CREAT | O_RDWR, 0644, &attr));
    if (mqd_ < 0) {
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::Open(const QueueConfig& config) noexcept {
    if (config.name.empty() || config.name[0] != '/') {
        return vr::core::ErrorCode::kInvalidParam;
    }

    queue_name_ = config.name;
    message_size_ = config.message_size;

    mqd_ = static_cast<int>(mq_open(queue_name_.c_str(), O_RDWR));
    if (mqd_ < 0) {
        return vr::core::ErrorCode::kQueueCreateFailed;
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::Send(const std::string& message, const std::uint32_t priority) noexcept {
    if (mqd_ < 0 || message.size() >= static_cast<std::size_t>(message_size_)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    const int rc = mq_send(static_cast<mqd_t>(mqd_), message.c_str(), message.size() + 1U, priority);
    if (rc != 0) {
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::Receive(std::string* const message, std::uint32_t* const priority) noexcept {
    if (mqd_ < 0 || message == nullptr || priority == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::vector<char> buffer(static_cast<std::size_t>(message_size_), '\0');
    const ssize_t bytes = mq_receive(static_cast<mqd_t>(mqd_), buffer.data(),
                                     static_cast<std::size_t>(message_size_), priority);
    if (bytes < 0) {
        return vr::core::ErrorCode::kQueueReceiveFailed;
    }

    message->assign(buffer.data());
    return vr::core::ErrorCode::kOk;
}

void PosixMessageQueue::Close() noexcept {
    if (mqd_ >= 0) {
        (void)mq_close(static_cast<mqd_t>(mqd_));
        mqd_ = -1;
    }
}

void PosixMessageQueue::Unlink() noexcept {
    if (!queue_name_.empty()) {
        (void)mq_unlink(queue_name_.c_str());
    }
}

}  // namespace ipc
}  // namespace vr

