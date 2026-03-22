#include "ipc/posix_message_queue.hpp"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <vector>

namespace vr {
namespace ipc {

namespace {

// 将相对超时（ms）转换为 POSIX 绝对超时（timespec）。
// 说明：mq_timedsend/mq_timedreceive 使用“绝对时间”，不是相对时长。
timespec MakeAbsTimeout(const std::int64_t timeout_ms) noexcept {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);

    const std::int64_t sec = timeout_ms / 1000;
    const std::int64_t nsec = (timeout_ms % 1000) * 1000000;

    ts.tv_sec += static_cast<time_t>(sec);
    ts.tv_nsec += static_cast<long>(nsec);

    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return ts;
}

// 切换队列非阻塞模式，并返回原始 flags 以便恢复。
// 注意：失败时必须保持调用方可感知，避免“半切换”造成不可预测阻塞。
bool SetQueueNonBlocking(const int mqd, bool enable, long* original_flags) noexcept {
    if (original_flags == nullptr) {
        return false;
    }

    mq_attr attr{};
    if (mq_getattr(static_cast<mqd_t>(mqd), &attr) != 0) {
        return false;
    }

    *original_flags = attr.mq_flags;
    if (enable) {
        attr.mq_flags = attr.mq_flags | O_NONBLOCK;
    } else {
        attr.mq_flags = attr.mq_flags & (~O_NONBLOCK);
    }

    if (mq_setattr(static_cast<mqd_t>(mqd), &attr, nullptr) != 0) {
        return false;
    }
    return true;
}

void RestoreQueueFlags(const int mqd, const long original_flags) noexcept {
    mq_attr restore_attr{};
    restore_attr.mq_flags = original_flags;
    (void)mq_setattr(static_cast<mqd_t>(mqd), &restore_attr, nullptr);
}

}  // namespace

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

vr::core::ErrorCode PosixMessageQueue::Send(const std::string& message,
                                            const std::uint32_t priority) noexcept {
    return SendWithTimeout(message, priority, -1);
}

vr::core::ErrorCode PosixMessageQueue::SendNonBlocking(const std::string& message,
                                                       const std::uint32_t priority) noexcept {
    if (mqd_ < 0 || message.size() >= static_cast<std::size_t>(message_size_)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    long original_flags = 0;
    if (!SetQueueNonBlocking(mqd_, true, &original_flags)) {
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    const int rc = mq_send(static_cast<mqd_t>(mqd_), message.c_str(), message.size() + 1U, priority);
    RestoreQueueFlags(mqd_, original_flags);

    if (rc != 0) {
        if (errno == EAGAIN) {
            return vr::core::ErrorCode::kWouldBlock;
        }
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::SendWithTimeout(const std::string& message,
                                                       const std::uint32_t priority,
                                                       const std::int64_t timeout_ms) noexcept {
    if (mqd_ < 0 || message.size() >= static_cast<std::size_t>(message_size_)) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    if (timeout_ms < 0) {
        const int rc = mq_send(static_cast<mqd_t>(mqd_), message.c_str(), message.size() + 1U,
                               priority);
        if (rc != 0) {
            return vr::core::ErrorCode::kQueueSendFailed;
        }
        return vr::core::ErrorCode::kOk;
    }

    const timespec abs_timeout = MakeAbsTimeout(timeout_ms);
    const int rc = mq_timedsend(static_cast<mqd_t>(mqd_), message.c_str(), message.size() + 1U,
                                priority, &abs_timeout);
    if (rc != 0) {
        if (errno == ETIMEDOUT) {
            return vr::core::ErrorCode::kTimeout;
        }
        if (errno == EAGAIN) {
            return vr::core::ErrorCode::kWouldBlock;
        }
        return vr::core::ErrorCode::kQueueSendFailed;
    }

    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::Receive(std::string* const message,
                                               std::uint32_t* const priority) noexcept {
    return ReceiveWithTimeout(message, priority, -1);
}

vr::core::ErrorCode PosixMessageQueue::ReceiveNonBlocking(std::string* const message,
                                                          std::uint32_t* const priority) noexcept {
    if (mqd_ < 0 || message == nullptr || priority == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    long original_flags = 0;
    if (!SetQueueNonBlocking(mqd_, true, &original_flags)) {
        return vr::core::ErrorCode::kQueueReceiveFailed;
    }

    std::vector<char> buffer(static_cast<std::size_t>(message_size_), '\0');
    const ssize_t bytes = mq_receive(static_cast<mqd_t>(mqd_), buffer.data(),
                                     static_cast<std::size_t>(message_size_), priority);
    RestoreQueueFlags(mqd_, original_flags);

    if (bytes < 0) {
        if (errno == EAGAIN) {
            return vr::core::ErrorCode::kWouldBlock;
        }
        return vr::core::ErrorCode::kQueueReceiveFailed;
    }

    message->assign(buffer.data());
    return vr::core::ErrorCode::kOk;
}

vr::core::ErrorCode PosixMessageQueue::ReceiveWithTimeout(std::string* const message,
                                                          std::uint32_t* const priority,
                                                          const std::int64_t timeout_ms) noexcept {
    if (mqd_ < 0 || message == nullptr || priority == nullptr) {
        return vr::core::ErrorCode::kInvalidParam;
    }

    std::vector<char> buffer(static_cast<std::size_t>(message_size_), '\0');

    if (timeout_ms < 0) {
        const ssize_t bytes = mq_receive(static_cast<mqd_t>(mqd_), buffer.data(),
                                         static_cast<std::size_t>(message_size_), priority);
        if (bytes < 0) {
            return vr::core::ErrorCode::kQueueReceiveFailed;
        }
        message->assign(buffer.data());
        return vr::core::ErrorCode::kOk;
    }

    const timespec abs_timeout = MakeAbsTimeout(timeout_ms);
    const ssize_t bytes = mq_timedreceive(static_cast<mqd_t>(mqd_), buffer.data(),
                                          static_cast<std::size_t>(message_size_), priority,
                                          &abs_timeout);
    if (bytes < 0) {
        if (errno == ETIMEDOUT) {
            return vr::core::ErrorCode::kTimeout;
        }
        if (errno == EAGAIN) {
            return vr::core::ErrorCode::kWouldBlock;
        }
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
