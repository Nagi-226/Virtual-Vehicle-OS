#ifndef COMMONSVC_LOG_LOGGER_HPP
#define COMMONSVC_LOG_LOGGER_HPP

#include <atomic>
#include <fstream>
#include <mutex>
#include <string>

#include "core/error_code.hpp"

namespace commonsvc {

enum class LogLevel {
    kDebug = 0,
    kInfo = 1,
    kWarn = 2,
    kError = 3
};

class Logger {
public:
    static Logger& Instance() noexcept;

    void SetMinLevel(LogLevel level) noexcept;
    void EnableConsole(bool enabled) noexcept;
    bool SetOutputFile(const std::string& file_path) noexcept;
    void SetDefaultContext(const std::string& module, const std::string& context) noexcept;

    void SetThreadContext(const std::string& module, const std::string& context) noexcept;
    void ClearThreadContext() noexcept;
    bool GetThreadContext(std::string* module, std::string* context) const noexcept;

    void Log(LogLevel level, const std::string& message, const char* file, int line);
    void LogWithCode(LogLevel level, vr::core::ErrorCode error_code, const std::string& message,
                     const char* file, int line);

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void WriteLogLine(LogLevel level, vr::core::ErrorCode error_code, const std::string& message,
                      const char* file, int line);
    void ResolveEffectiveContext(std::string* module, std::string* context) const noexcept;
    static const char* ToString(LogLevel level) noexcept;
    static std::string CurrentTimestamp();

    std::atomic<LogLevel> min_level_{LogLevel::kInfo};
    std::atomic<bool> console_enabled_{true};
    mutable std::mutex output_mutex_;
    std::ofstream file_stream_;
    std::string default_module_{"general"};
    std::string default_context_{"default"};
};

class ScopedLogContext {
public:
    ScopedLogContext(const std::string& module, const std::string& context) noexcept;
    ~ScopedLogContext();

    ScopedLogContext(const ScopedLogContext&) = delete;
    ScopedLogContext& operator=(const ScopedLogContext&) = delete;

private:
    bool had_previous_{false};
    std::string previous_module_;
    std::string previous_context_;
};

}  // namespace commonsvc

#define LOG_DEBUG(msg) \
    ::commonsvc::Logger::Instance().Log(::commonsvc::LogLevel::kDebug, (msg), __FILE__, __LINE__)

#define LOG_INFO(msg) \
    ::commonsvc::Logger::Instance().Log(::commonsvc::LogLevel::kInfo, (msg), __FILE__, __LINE__)

#define LOG_WARN(msg) \
    ::commonsvc::Logger::Instance().Log(::commonsvc::LogLevel::kWarn, (msg), __FILE__, __LINE__)

#define LOG_ERROR(msg) \
    ::commonsvc::Logger::Instance().Log(::commonsvc::LogLevel::kError, (msg), __FILE__, __LINE__)

#define LOG_ERROR_CODE(code, msg)                                                             \
    ::commonsvc::Logger::Instance().LogWithCode(::commonsvc::LogLevel::kError, (code), (msg), \
                                                __FILE__, __LINE__)

#endif  // COMMONSVC_LOG_LOGGER_HPP
