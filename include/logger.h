/*************************************************************************
    > File Name: logger.h
    > Author: hsz
    > Brief: Unified logging callback and level control interface for couring
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_LOGGER_H__
#define __COURING_LOGGER_H__

#include <cstdint>
#include <functional>
#include <cstdlib>

namespace couring {

enum class LogLevel : uint8_t {
    DEBUG  = 0,
    INFO   = 1,
    WARN   = 2,
    ERROR  = 3,
    FATAL  = 4,
    SILENT = 5   // 静默，关闭所有日志输出
};

using LogCallback = std::function<void(LogLevel level, const char* tag, const char* msg)>;

void set_log_level(LogLevel level) noexcept;
LogLevel get_log_level() noexcept;

void set_log_callback(LogCallback cb) noexcept;
void reset_log_callback() noexcept;

bool is_log_enabled(LogLevel level) noexcept;

const char* log_level_to_string(LogLevel level) noexcept;

void log_write(LogLevel level, const char* tag, const char* fmt, ...) noexcept;

} // namespace couring

namespace eular {
    using LogLevel = ::couring::LogLevel;
    using LogCallback = ::couring::LogCallback;
    using ::couring::set_log_level;
    using ::couring::get_log_level;
    using ::couring::set_log_callback;
    using ::couring::reset_log_callback;
    using ::couring::is_log_enabled;
    using ::couring::log_write;
}

#endif // __COURING_LOGGER_H__
