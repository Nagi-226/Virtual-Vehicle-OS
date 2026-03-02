#include "log/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace commonsvc {

namespace {
thread_local bool g_has_thread_context = false;
thread_local std::string g_thread_module;
thread_local std::string g_thread_context;
}  // namespace

Logger& Logger::Instance() noexcept {
    static Logger instance;
    return instance;
}

void Logger::SetMinLevel(const LogLevel level) noexcept {
    min_level_.store(level, std::memory_order_relaxed);
}

void Logger::EnableConsole(const bool enabled) noexcept {
    console_enabled_.store(enabled, std::memory_order_relaxed);
}

bool Logger::SetOutputFile(const std::string& file_path) noexcept {
    std::lock_guard<std::mutex> lock(output_mutex_);

    if (file_stream_.is_open()) {
        file_stream_.close();
    }

    file_stream_.open(file_path, std::ios::out | std::ios::app);
    return file_stream_.is_open();
}

void Logger::SetDefaultContext(const std::string& module, const std::string& context) noexcept {
    std::lock_guard<std::mutex> lock(output_mutex_);
    default_module_ = module;
    default_context_ = context;
}

void Logger::SetThreadContext(const std::string& module, const std::string& context) noexcept {
    g_thread_module = module;
    g_thread_context = context;
    g_has_thread_context = true;
}

void Logger::ClearThreadContext() noexcept {
    g_thread_module.clear();
    g_thread_context.clear();
    g_has_thread_context = false;
}

bool Logger::GetThreadContext(std::string* const module, std::string* const context) const noexcept {
    if (!g_has_thread_context || module == nullptr || context == nullptr) {
        return false;
    }
    *module = g_thread_module;
    *context = g_thread_context;
    return true;
}

void Logger::Log(const LogLevel level, const std::string& message, const char* const file,
                 const int line) {
    WriteLogLine(level, vr::core::ErrorCode::kOk, message, file, line);
}

void Logger::LogWithCode(const LogLevel level, const vr::core::ErrorCode error_code,
                         const std::string& message, const char* const file, const int line) {
    WriteLogLine(level, error_code, message, file, line);
}

void Logger::ResolveEffectiveContext(std::string* const module, std::string* const context) const noexcept {
    if (module == nullptr || context == nullptr) {
        return;
    }

    if (g_has_thread_context) {
        *module = g_thread_module;
        *context = g_thread_context;
        return;
    }

    std::lock_guard<std::mutex> lock(output_mutex_);
    *module = default_module_;
    *context = default_context_;
}

void Logger::WriteLogLine(const LogLevel level, const vr::core::ErrorCode error_code,
                          const std::string& message, const char* const file, const int line) {
    if (level < min_level_.load(std::memory_order_relaxed)) {
        return;
    }

    std::string module;
    std::string context;
    ResolveEffectiveContext(&module, &context);

    std::ostringstream oss;
    oss << CurrentTimestamp() << " [" << ToString(level) << "]"
        << " module=" << module << " context=" << context
        << " error_domain=" << vr::core::ToDomainString(error_code)
        << " error_code=0x" << std::hex << static_cast<std::uint32_t>(error_code) << std::dec
        << " [tid=" << std::this_thread::get_id() << "]"
        << " [" << file << ":" << line << "] " << message;

    std::lock_guard<std::mutex> lock(output_mutex_);

    if (console_enabled_.load(std::memory_order_relaxed)) {
        std::cout << oss.str() << std::endl;
    }

    if (file_stream_.is_open()) {
        file_stream_ << oss.str() << std::endl;
        file_stream_.flush();
    }
}

const char* Logger::ToString(const LogLevel level) noexcept {
    switch (level) {
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string Logger::CurrentTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &now_time);
#else
    localtime_r(&now_time, &tm_snapshot);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_snapshot, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

ScopedLogContext::ScopedLogContext(const std::string& module, const std::string& context) noexcept {
    had_previous_ = Logger::Instance().GetThreadContext(&previous_module_, &previous_context_);
    Logger::Instance().SetThreadContext(module, context);
}

ScopedLogContext::~ScopedLogContext() {
    if (had_previous_) {
        Logger::Instance().SetThreadContext(previous_module_, previous_context_);
    } else {
        Logger::Instance().ClearThreadContext();
    }
}

}  // namespace commonsvc
