#include <chrono>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "core/process_guardian.hpp"
#include "core/thread_pool.hpp"
#include "ipc/posix_message_queue.hpp"
#include "log/logger.hpp"

namespace {

/**
 * @brief 子服务入口：模拟车载/机器人基础服务生命周期。
 * @return 0 表示成功退出，非0表示异常退出。
 */
int ServiceEntry() {
    using vr::core::ErrorCode;
    using vr::ipc::PosixMessageQueue;

    // 设置日志级别、输出到控制台、设置默认上下文
    commonsvc::Logger::Instance().SetMinLevel(commonsvc::LogLevel::kDebug);
    commonsvc::Logger::Instance().EnableConsole(true);
    commonsvc::Logger::Instance().SetDefaultContext("demo", "service_entry");

    LOG_INFO("Service starting...");

    // 配置 POSIX 消息队列的“基本参数”
    vr::ipc::QueueConfig queue_config;
    queue_config.name = "/vr_framework_demo_queue";
    queue_config.max_messages = 10;
    queue_config.message_size = 256;

    PosixMessageQueue queue;
    ErrorCode ec = queue.Create(queue_config);
    if (ec != ErrorCode::kOk) {
        LOG_ERROR_CODE(ec, std::string("Queue create failed: ") + vr::core::ToString(ec));
        return 1;
    }

    vr::core::ThreadPool pool;
    vr::core::ThreadConfig thread_config;
    thread_config.worker_count = 2;
    thread_config.enable_realtime = false;
    thread_config.queue_capacity = 1;
    thread_config.rejection_policy = vr::core::RejectionPolicy::kCallerRuns;

    // 启动线程池
    ec = pool.Start(thread_config);
    if (ec != ErrorCode::kOk) {
        LOG_ERROR_CODE(ec, std::string("ThreadPool start failed: ") + vr::core::ToString(ec));
        queue.Close();
        queue.Unlink();
        return 2;
    }

    // 投递发送任务
    ec = pool.Enqueue([&queue]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        const auto send_ec = queue.Send("hello_from_sender", 1U);
        if (send_ec != ErrorCode::kOk) {
            LOG_ERROR_CODE(send_ec, std::string("Send failed: ") + vr::core::ToString(send_ec));
            return;
        }
        LOG_INFO("Message sent to POSIX queue");
    });

    // 检查投递结果（发送任务有没有成功入队）
    if (ec != ErrorCode::kOk) {
        LOG_ERROR_CODE(ec, std::string("Enqueue sender failed: ") + vr::core::ToString(ec));
        pool.Stop();
        queue.Close();
        queue.Unlink();
        return 3;
    }

    // 投递接收任务
    ec = pool.Enqueue([&queue]() {
        std::string msg;
        std::uint32_t prio = 0;
        const auto recv_ec = queue.Receive(&msg, &prio);
        if (recv_ec != ErrorCode::kOk) {
            LOG_ERROR_CODE(recv_ec, std::string("Receive failed: ") + vr::core::ToString(recv_ec));
            return;
        }

        LOG_INFO("Received message: " + msg + ", priority=" + std::to_string(prio));
    });

    if (ec != ErrorCode::kOk && ec != ErrorCode::kThreadTaskRejected) {
        LOG_ERROR_CODE(ec, std::string("Enqueue receiver failed: ") + vr::core::ToString(ec));
        pool.Stop();
        queue.Close();
        queue.Unlink();
        return 4;
    }

    if (ec == ErrorCode::kThreadTaskRejected) {
        LOG_WARN("Queue full triggered rejection policy: caller runs");
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    const vr::core::ThreadPoolMetrics metrics = pool.GetMetrics();
    LOG_INFO("ThreadPool metrics => queue=" + std::to_string(metrics.queue_size) + "/" +
             std::to_string(metrics.queue_capacity) + ", submitted=" +
             std::to_string(metrics.submitted_count) + ", executed=" +
             std::to_string(metrics.executed_count) + ", rejected=" +
             std::to_string(metrics.rejected_count));

    pool.Stop();
    queue.Close();
    queue.Unlink();

    LOG_INFO("Service exit normally");
    return 0;
}

}  // namespace

/**
 * @brief 程序主入口，调用进程守护模块执行服务。
 * @return 0 表示演示成功。
 */
int main() {
    commonsvc::Logger::Instance().SetDefaultContext("demo", "main");

    vr::core::ProcessGuardian guardian;
    int final_status = -1;

    const vr::core::ErrorCode ec = guardian.RunWithRestart(ServiceEntry, 0, &final_status);
    if (ec != vr::core::ErrorCode::kOk || final_status != 0) {
        LOG_ERROR_CODE(ec, "Service guardian failed");
        return 1;
    }

    LOG_INFO("Demo finished successfully");
    return 0;
}
