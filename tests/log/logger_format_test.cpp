#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "core/error_code.hpp"
#include "log/logger.hpp"

namespace {

bool ExpectTrue(const bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAILED] " << message << std::endl;
        return false;
    }
    return true;
}

std::string CaptureStdout(const std::function<void()>& log_action) {
    std::ostringstream capture;
    auto* original_buffer = std::cout.rdbuf(capture.rdbuf());
    log_action();
    std::cout.rdbuf(original_buffer);
    return capture.str();
}

bool ContainsField(const std::string& line, const std::string& field) {
    return line.find(field) != std::string::npos;
}

bool TestInfoLogFields() {
    auto& logger = commonsvc::Logger::Instance();
    logger.SetMinLevel(commonsvc::LogLevel::kDebug);
    logger.EnableConsole(true);
    logger.SetDefaultContext("test_module", "ctx_info");

    const std::string line = CaptureStdout([]() { LOG_INFO("info_message"); });

    return ExpectTrue(ContainsField(line, "[INFO]"), "INFO level field missing") &&
           ExpectTrue(ContainsField(line, "module=test_module"), "module field missing") &&
           ExpectTrue(ContainsField(line, "context=ctx_info"), "context field missing") &&
           ExpectTrue(ContainsField(line, "error_domain=COMMON"), "error_domain field missing") &&
           ExpectTrue(ContainsField(line, "error_code=0x0"), "error_code field missing") &&
           ExpectTrue(ContainsField(line, "[tid="), "tid field missing") &&
           ExpectTrue(ContainsField(line, "logger_format_test.cpp:"), "file:line field missing") &&
           ExpectTrue(ContainsField(line, "info_message"), "message field missing");
}

bool TestErrorCodeLogFields() {
    auto& logger = commonsvc::Logger::Instance();
    logger.SetMinLevel(commonsvc::LogLevel::kDebug);
    logger.SetDefaultContext("ipc_module", "ctx_error");

    const std::string line =
        CaptureStdout([]() { LOG_ERROR_CODE(vr::core::ErrorCode::kQueueReceiveFailed, "receive_failed"); });

    return ExpectTrue(ContainsField(line, "[ERROR]"), "ERROR level field missing") &&
           ExpectTrue(ContainsField(line, "module=ipc_module"), "module field missing") &&
           ExpectTrue(ContainsField(line, "context=ctx_error"), "context field missing") &&
           ExpectTrue(ContainsField(line, "error_domain=IPC"), "error_domain mismatch") &&
           ExpectTrue(ContainsField(line, "error_code=0x20003"), "error_code mismatch") &&
           ExpectTrue(ContainsField(line, "receive_failed"), "error message missing");
}

bool TestLevelFilter() {
    auto& logger = commonsvc::Logger::Instance();
    logger.SetMinLevel(commonsvc::LogLevel::kError);

    const std::string line = CaptureStdout([]() { LOG_DEBUG("debug_message"); });

    logger.SetMinLevel(commonsvc::LogLevel::kDebug);
    return ExpectTrue(line.empty(), "DEBUG should be filtered when log level is ERROR");
}

bool TestScopedLogContextThreadLocalOverride() {
    auto& logger = commonsvc::Logger::Instance();
    logger.SetMinLevel(commonsvc::LogLevel::kDebug);
    logger.SetDefaultContext("default_module", "default_ctx");

    std::string worker_line;
    std::thread worker([&worker_line]() {
        worker_line = CaptureStdout([]() {
            const commonsvc::ScopedLogContext scoped("worker_module", "worker_ctx");
            LOG_INFO("worker_message");
        });
    });
    worker.join();

    const std::string main_line = CaptureStdout([]() { LOG_INFO("main_message"); });

    return ExpectTrue(ContainsField(worker_line, "module=worker_module"),
                      "worker module should use scoped context") &&
           ExpectTrue(ContainsField(worker_line, "context=worker_ctx"),
                      "worker context should use scoped context") &&
           ExpectTrue(ContainsField(main_line, "module=default_module"),
                      "main thread should keep default module") &&
           ExpectTrue(ContainsField(main_line, "context=default_ctx"),
                      "main thread should keep default context");
}

}  // namespace

int main() {
    const bool ok = TestInfoLogFields() && TestErrorCodeLogFields() && TestLevelFilter() &&
                    TestScopedLogContextThreadLocalOverride();

    if (!ok) {
        std::cerr << "Logger format tests failed." << std::endl;
        return 1;
    }

    std::cout << "Logger format tests passed." << std::endl;
    return 0;
}
