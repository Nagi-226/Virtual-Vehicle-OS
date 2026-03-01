#include "log/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace vr {
namespace log {

Logger& Logger::Instance() noexcept {
    static Logger instance;
    return instance;
}

void Logger::SetLevel(const LogLevel level) noexcept {
    level_.store(level, std::memory_order_relaxed);
}

void Logger::Debug(const std::string& module, const std::string& message) {
    Log(LogLevel::kDebug, module, message);
}

void Logger::Info(const std::string& module, const std::string& message) {
    Log(LogLevel::kInfo, module, message);
}

void Logger::Error(const std::string& module, const std::string& message) {
    Log(LogLevel::kError, module, message);
}

void Logger::Log(const LogLevel level, const std::string& module, const std::string& message) {
    if (level < level_.load(std::memory_order_relaxed)) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &now_time);
#else
    localtime_r(&now_time, &tm_snapshot);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S")
        << " [" << LevelToString(level) << "]"
        << " [tid=" << std::this_thread::get_id() << "]"
        << " [" << module << "] "
        << message;

    std::lock_guard<std::mutex> lock(output_mutex_);
    std::cout << oss.str() << std::endl;
}

const char* Logger::LevelToString(const LogLevel level) const noexcept {
    switch (level) {
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kError:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

}  // namespace log
}  // namespace vr

