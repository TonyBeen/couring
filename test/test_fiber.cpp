#include "catch.hpp"
#include "fiber.h"

#include <vector>
#include <string>

TEST_CASE("Fiber basic execution and completion", "[fiber]") {
    using namespace eular;

    bool executed = false;
    uint64_t executedFiberId = 0;

    Fiber::SP fiber(new (std::nothrow) Fiber([&]() noexcept {
        executed = true;
        executedFiberId = Fiber::GetFiberID();
    }));

    REQUIRE(fiber != nullptr);
    REQUIRE(fiber->getState() == Fiber::READY);
    REQUIRE(fiber->getFiberId() > 0);

    fiber->resume();

    REQUIRE(executed == true);
    REQUIRE(executedFiberId == fiber->getFiberId());
    REQUIRE(fiber->getState() == Fiber::TERM);
}

TEST_CASE("Fiber Yield2Hold and resume", "[fiber]") {
    using namespace eular;

    std::vector<int32_t> steps;

    Fiber::SP fiber(new (std::nothrow) Fiber([&]() noexcept {
        steps.push_back(1);
        Fiber::Yield2Hold();
        steps.push_back(2);
        Fiber::Yield2Hold();
        steps.push_back(3);
    }));

    REQUIRE(fiber != nullptr);
    REQUIRE(steps.empty());

    fiber->resume();
    REQUIRE(steps == std::vector<int32_t>{1});
    REQUIRE(fiber->getState() == Fiber::HOLD);

    fiber->resume();
    REQUIRE(steps == std::vector<int32_t>{1, 2});
    REQUIRE(fiber->getState() == Fiber::HOLD);

    fiber->resume();
    REQUIRE(steps == std::vector<int32_t>{1, 2, 3});
    REQUIRE(fiber->getState() == Fiber::TERM);
}

TEST_CASE("Fiber reset and reuse", "[fiber]") {
    using namespace eular;

    int32_t counter = 0;

    Fiber::SP fiber(new (std::nothrow) Fiber([&]() noexcept {
        counter += 10;
    }));

    REQUIRE(fiber != nullptr);
    fiber->resume();
    REQUIRE(counter == 10);
    REQUIRE(fiber->getState() == Fiber::TERM);

    fiber->reset([&]() noexcept {
        counter += 20;
    });
    REQUIRE(fiber->getState() == Fiber::READY);

    fiber->resume();
    REQUIRE(counter == 30);
    REQUIRE(fiber->getState() == Fiber::TERM);
}

#include "logger.h"

TEST_CASE("Fiber logging inside coroutine with 16KB stack", "[fiber]") {
    using namespace eular;

    std::string loggedMessage;
    couring::set_log_callback([&](couring::LogLevel, const char*, const char* msg) {
        loggedMessage = msg;
    });
    couring::set_log_level(couring::LogLevel::DEBUG);

    Fiber::SP fiber(new (std::nothrow) Fiber([&]() noexcept {
        couring::log_write(couring::LogLevel::INFO, "FiberTest", "Logging from fiber %lu with 16KB stack", Fiber::GetFiberID());
    }));

    REQUIRE(fiber != nullptr);
    fiber->resume();
    REQUIRE(fiber->getState() == Fiber::TERM);
    REQUIRE(loggedMessage.find("Logging from fiber") != std::string::npos);

    couring::reset_log_callback();
    couring::set_log_level(couring::LogLevel::INFO);
}
