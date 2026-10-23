#include "catch.hpp"
#include "scheduler.h"

#include <vector>

TEST_CASE("Scheduler runs multiple tasks and stops gracefully", "[scheduler]") {
    using namespace eular;

    Scheduler sc("TestScheduler");
    std::vector<int32_t> sequence;

    sc.schedule([&]() noexcept {
        sequence.push_back(1);
    });

    sc.schedule([&]() noexcept {
        sequence.push_back(2);
    });

    sc.schedule([&]() noexcept {
        sequence.push_back(3);
        sc.stop();
    });

    sc.start();

    REQUIRE(sequence == std::vector<int32_t>{1, 2, 3});
}

TEST_CASE("Scheduler schedules fibers with Yield2Ready", "[scheduler]") {
    using namespace eular;

    Scheduler sc("TestSchedulerYield");
    std::vector<int32_t> results;

    Fiber::SP f1(new (std::nothrow) Fiber([&]() noexcept {
        results.push_back(100);
        Fiber::Yield2Ready();
        results.push_back(101);
    }));

    Fiber::SP f2(new (std::nothrow) Fiber([&]() noexcept {
        results.push_back(200);
        Fiber::Yield2Ready();
        results.push_back(201);
        sc.stop();
    }));

    sc.schedule(f1);
    sc.schedule(f2);
    sc.start();

    REQUIRE(results == std::vector<int32_t>{100, 200, 101, 201});
}

#include "uring_service.h"
#include <thread>
#include <atomic>

TEST_CASE("UringService cross-thread post and wakeup", "[scheduler_post]") {
    using namespace eular;

    UringService service(64);
    REQUIRE(service.isValid());

    std::atomic<bool> taskExecuted{false};
    std::atomic<bool> serviceStarted{false};

    std::thread worker([&]() {
        serviceStarted = true;
        // Occupies this worker thread, like libevent's event_base_dispatch
        service.start();
    });

    while (!serviceStarted) {
        std::this_thread::yield();
    }
    // Give the worker thread a moment to enter the wait loop
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Post a task from the main thread to the worker thread's event loop
    service.post([&]() {
        taskExecuted = true;
        service.stop();
    });

    worker.join();
    REQUIRE(taskExecuted == true);
}
