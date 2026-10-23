/*************************************************************************
    > File Name: logger.cpp
    > Author: hsz
    > Brief: Unified logging implementation with level filtering and callback
    > Created Time: 2026-10-23
 ************************************************************************/

#include "logger.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <ctime>
#include <atomic>

namespace couring {

static std::atomic<uint8_t> g_log_level{static_cast<uint8_t>(LogLevel::INFO)};
static LogCallback g_log_callback = nullptr;

void set_log_level(LogLevel level) noexcept
{
    g_log_level.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

LogLevel get_log_level() noexcept
{
    return static_cast<LogLevel>(g_log_level.load(std::memory_order_relaxed));
}

void set_log_callback(LogCallback cb) noexcept
{
    try {
        g_log_callback = std::move(cb);
    } catch (...) {
    }
}

void reset_log_callback() noexcept
{
    g_log_callback = nullptr;
}

bool is_log_enabled(LogLevel level) noexcept
{
    if (level >= LogLevel::SILENT) {
        return false;
    }
    uint8_t cur = g_log_level.load(std::memory_order_relaxed);
    if (cur >= static_cast<uint8_t>(LogLevel::SILENT)) {
        return false;
    }
    return static_cast<uint8_t>(level) >= cur;
}

const char* log_level_to_string(LogLevel level) noexcept
{
    switch (level) {
        case LogLevel::DEBUG:  return "DEBUG";
        case LogLevel::INFO:   return "INFO";
        case LogLevel::WARN:   return "WARN";
        case LogLevel::ERROR:  return "ERROR";
        case LogLevel::FATAL:  return "FATAL";
        case LogLevel::SILENT: return "SILENT";
        default:               return "UNKNOWN";
    }
}

static void default_log_sink(LogLevel level, const char* tag, const char* msg) noexcept
{
    time_t now = time(nullptr);
    struct tm tm_buf;
    localtime_r(&now, &tm_buf);

    char time_str[64];
    snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    FILE* out = (level >= LogLevel::WARN) ? stderr : stdout;
    fprintf(out, "%s [%s] [%s] %s\n", time_str, log_level_to_string(level), tag ? tag : "couring", msg);
    fflush(out);
}

void log_write(LogLevel level, const char* tag, const char* fmt, ...) noexcept
{
    if (!is_log_enabled(level)) {
        return;
    }

    // Thread-local formatting buffer to avoid consuming coroutine stack space
    static thread_local char s_log_buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_log_buf, sizeof(s_log_buf), fmt, args);
    va_end(args);

    try {
        if (g_log_callback) {
            g_log_callback(level, tag, s_log_buf);
        } else {
            default_log_sink(level, tag, s_log_buf);
        }
    } catch (...) {
        // Guarantee zero-exception even if user callback throws
    }
}

} // namespace couring
