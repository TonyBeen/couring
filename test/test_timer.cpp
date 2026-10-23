#include "catch.hpp"
#include "flat_timer_heap.h"
#include "uring_service.h"
#include "io/co_timer.h"

#include <vector>
#include <unistd.h>

TEST_CASE("FlatTimerHeap ordering and cancellation", "[timer]") {
    using namespace couring;

    FlatTimerHeap heap;
    std::vector<int32_t> executed;

    auto t3 = heap.addTimer(50, [&]() noexcept {
        executed.push_back(3);
    });

    auto t1 = heap.addTimer(10, [&]() noexcept {
        executed.push_back(1);
    });

    auto t2 = heap.addTimer(30, [&]() noexcept {
        executed.push_back(2);
    });

    REQUIRE(t1 != nullptr);
    REQUIRE(t2 != nullptr);
    REQUIRE(t3 != nullptr);

    // Cancel t2
    t2->cancel();

    // Sleep 20ms and process
    usleep(20000);
    heap.processExpiredTimers();
    REQUIRE(executed == std::vector<int32_t>{1});

    // Sleep another 40ms and process
    usleep(40000);
    heap.processExpiredTimers();
    REQUIRE(executed == std::vector<int32_t>{1, 3});
}

TEST_CASE("co_msleep inside UringService", "[timer]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    bool slept = false;
    uint64_t startMs = FlatTimerHeap::CurrentTimeMs();

    service.schedule([&]() noexcept {
        int32_t ret = co_msleep(30);
        REQUIRE(ret == 0);
        slept = true;
        service.stop();
    });

    service.start();

    uint64_t elapsed = FlatTimerHeap::CurrentTimeMs() - startMs;
    REQUIRE(slept == true);
    REQUIRE(elapsed >= 25);
}
