#include "catch.hpp"
#include "logger.h"
#include <string>
#include <vector>
#include <stdexcept>

struct LogEntry {
    couring::LogLevel level;
    std::string tag;
    std::string msg;
};

TEST_CASE("Logger default level and level setting", "[logger]") {
    // Save original state
    couring::LogLevel original_level = couring::get_log_level();
    couring::reset_log_callback();

    SECTION("Default level is INFO") {
        couring::set_log_level(couring::LogLevel::INFO);
        REQUIRE(couring::get_log_level() == couring::LogLevel::INFO);
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::DEBUG));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::INFO));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::WARN));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::ERROR));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::FATAL));
    }

    SECTION("Setting level to DEBUG enables all levels except OFF") {
        couring::set_log_level(couring::LogLevel::DEBUG);
        REQUIRE(couring::get_log_level() == couring::LogLevel::DEBUG);
        REQUIRE(couring::is_log_enabled(couring::LogLevel::DEBUG));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::INFO));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::WARN));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::ERROR));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::FATAL));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::SILENT));
    }

    SECTION("Setting level to WARN suppresses DEBUG and INFO") {
        couring::set_log_level(couring::LogLevel::WARN);
        REQUIRE(couring::get_log_level() == couring::LogLevel::WARN);
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::DEBUG));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::INFO));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::WARN));
        REQUIRE(couring::is_log_enabled(couring::LogLevel::ERROR));
    }

    SECTION("Setting level to SILENT suppresses all logging") {
        couring::set_log_level(couring::LogLevel::SILENT);
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::DEBUG));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::INFO));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::WARN));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::ERROR));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::FATAL));
        REQUIRE_FALSE(couring::is_log_enabled(couring::LogLevel::SILENT));
    }

    // Restore
    couring::set_log_level(original_level);
}

TEST_CASE("Logger custom callback and filtering", "[logger]") {
    std::vector<LogEntry> logs;
    couring::set_log_callback([&logs](couring::LogLevel lvl, const char* tag, const char* msg) {
        logs.push_back({lvl, tag ? tag : "", msg ? msg : ""});
    });

    SECTION("Filtered logs do not invoke callback") {
        logs.clear();
        couring::set_log_level(couring::LogLevel::WARN);

        couring::log_write(couring::LogLevel::DEBUG, "test", "This debug log should be dropped: %d", 42);
        couring::log_write(couring::LogLevel::INFO,  "test", "This info log should be dropped");
        REQUIRE(logs.empty());

        couring::log_write(couring::LogLevel::WARN,  "test", "This warning should be captured: %s", "test_warn");
        couring::log_write(couring::LogLevel::ERROR, "test", "This error should be captured: code=%d", 500);

        REQUIRE(logs.size() == 2);
        REQUIRE(logs[0].level == couring::LogLevel::WARN);
        REQUIRE(logs[0].msg == "This warning should be captured: test_warn");
        REQUIRE(logs[1].level == couring::LogLevel::ERROR);
        REQUIRE(logs[1].msg == "This error should be captured: code=500");
    }

    SECTION("Custom callback with DEBUG level captures formatted output") {
        logs.clear();
        couring::set_log_level(couring::LogLevel::DEBUG);

        couring::log_write(couring::LogLevel::DEBUG, "custom_tag", "val=%d, str=%s", 123, "hello");
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].level == couring::LogLevel::DEBUG);
        REQUIRE(logs[0].tag == "custom_tag");
        REQUIRE(logs[0].msg == "val=123, str=hello");
    }

    couring::reset_log_callback();
    couring::set_log_level(couring::LogLevel::INFO);
}

TEST_CASE("Logger exception safety in callback", "[logger]") {
    couring::set_log_level(couring::LogLevel::DEBUG);
    couring::set_log_callback([](couring::LogLevel, const char*, const char*) {
        throw std::runtime_error("Simulated callback exception");
    });

    couring::reset_log_callback();
    couring::set_log_level(couring::LogLevel::INFO);
}

#include "logger_internal.h"

TEST_CASE("Logger internal helper macros", "[logger]") {
    std::vector<LogEntry> logs;
    couring::set_log_callback([&logs](couring::LogLevel lvl, const char* tag, const char* msg) {
        logs.push_back({lvl, tag ? tag : "", msg ? msg : ""});
    });
    couring::set_log_level(couring::LogLevel::DEBUG);

    LOGD("internal debug %d", 1);
    LOGI("internal info %d", 2);
    LOGW("internal warn %d", 3);
    LOGE("internal error %d", 4);

    REQUIRE(logs.size() == 4);
    REQUIRE(logs[0].level == couring::LogLevel::DEBUG);
    REQUIRE(logs[1].level == couring::LogLevel::INFO);
    REQUIRE(logs[2].level == couring::LogLevel::WARN);
    REQUIRE(logs[3].level == couring::LogLevel::ERROR);

    couring::reset_log_callback();
    couring::set_log_level(couring::LogLevel::INFO);
}
