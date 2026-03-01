#ifndef VR_LOG_LOGGER_HPP
#define VR_LOG_LOGGER_HPP

#include <atomic>
#include <mutex>
#include <string>

namespace vr {
namespace log {

enum class LogLevel {
    kDebug = 0,
    kInfo = 1,
    kError = 2
};

class Logger {
public:
    static Logger& Instance() noexcept;

    void SetLevel(LogLevel level) noexcept;
    void Debug(const std::string& module, const std::string& message);
    void Info(const std::string& module, const std::string& message);
    void Error(const std::string& module, const std::string& message);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void Log(LogLevel level, const std::string& module, const std::string& message);
    const char* LevelToString(LogLevel level) const noexcept;

    std::atomic<LogLevel> level_{LogLevel::kDebug};
    std::mutex output_mutex_;
};

}  // namespace log
}  // namespace vr

#endif  // VR_LOG_LOGGER_HPP

