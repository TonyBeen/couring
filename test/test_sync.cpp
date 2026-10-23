#include "catch.hpp"
#include "scheduler.h"
#include "sync/co_wait_group.h"
#include "sync/co_semaphore.h"
#include "sync/co_channel.h"

#include <vector>
#include <string>

TEST_CASE("CoWaitGroup coordinates multiple fibers", "[sync][wait_group]") {
    using namespace eular;
    using namespace couring;

    Scheduler sc("TestWaitGroup");
    CoWaitGroup wg;
    std::vector<int32_t> finished;
    bool waitCompleted = false;

    constexpr int kWorkerCount = 5;
    wg.add(kWorkerCount);

    for (int i = 0; i < kWorkerCount; ++i) {
        Fiber::SP worker(new (std::nothrow) Fiber([&, i]() noexcept {
            // Simulate work and yield
            Fiber::Yield2Ready();
            finished.push_back(i);
            wg.done();
        }));
        sc.schedule(worker);
    }

    Fiber::SP master(new (std::nothrow) Fiber([&]() noexcept {
        wg.wait();
        waitCompleted = true;
        sc.stop();
    }));

    sc.schedule(master);
    sc.start();

    REQUIRE(waitCompleted == true);
    REQUIRE(finished.size() == kWorkerCount);
    REQUIRE(wg.count() == 0);
}

TEST_CASE("CoSemaphore limits concurrency and supports tryAcquire", "[sync][semaphore]") {
    using namespace eular;
    using namespace couring;

    Scheduler sc("TestSemaphore");
    CoSemaphore sem(2); // Allow 2 concurrent fibers
    REQUIRE(sem.available() == 2);

    int currentConcurrency = 0;
    int maxConcurrency = 0;
    int completedTasks = 0;

    constexpr int kFibers = 6;
    for (int i = 0; i < kFibers; ++i) {
        Fiber::SP f(new (std::nothrow) Fiber([&]() noexcept {
            sem.acquire(1);
            currentConcurrency++;
            if (currentConcurrency > maxConcurrency) {
                maxConcurrency = currentConcurrency;
            }

            // Yield to let other fibers attempt to acquire
            Fiber::Yield2Ready();

            currentConcurrency--;
            completedTasks++;
            sem.release(1);
        }));
        sc.schedule(f);
    }

    Fiber::SP watcher(new (std::nothrow) Fiber([&]() noexcept {
        while (completedTasks < kFibers) {
            Fiber::Yield2Ready();
        }
        sc.stop();
    }));

    sc.schedule(watcher);
    sc.start();

    REQUIRE(completedTasks == kFibers);
    REQUIRE(maxConcurrency == 2);
    REQUIRE(sem.available() == 2);

    // Test tryAcquire
    REQUIRE(sem.tryAcquire(2) == true);
    REQUIRE(sem.available() == 0);
    REQUIRE(sem.tryAcquire(1) == false);
    sem.release(2);
    REQUIRE(sem.available() == 2);
}

TEST_CASE("CoChannel buffered operation", "[sync][channel]") {
    using namespace eular;
    using namespace couring;

    Scheduler sc("TestChannelBuffered");
    CoChannel<int> chan(3);
    REQUIRE(chan.capacity() == 3);
    REQUIRE(chan.empty());

    std::vector<int> received;

    Fiber::SP producer(new (std::nothrow) Fiber([&]() noexcept {
        for (int i = 1; i <= 5; ++i) {
            chan.push(i);
        }
        chan.close();
    }));

    Fiber::SP consumer(new (std::nothrow) Fiber([&]() noexcept {
        int val = 0;
        while (chan.pop(val)) {
            received.push_back(val);
        }
        sc.stop();
    }));

    sc.schedule(consumer);
    sc.schedule(producer);
    sc.start();

    REQUIRE(received == std::vector<int>{1, 2, 3, 4, 5});
    REQUIRE(chan.isClosed());
}

TEST_CASE("CoChannel unbuffered rendezvous", "[sync][channel]") {
    using namespace eular;
    using namespace couring;

    Scheduler sc("TestChannelUnbuffered");
    CoChannel<std::string> chan(0); // Rendezvous
    REQUIRE(chan.capacity() == 0);

    std::vector<std::string> trace;

    Fiber::SP sender(new (std::nothrow) Fiber([&]() noexcept {
        trace.push_back("send_start_1");
        chan.push("hello");
        trace.push_back("send_done_1");

        trace.push_back("send_start_2");
        // Receiver is not waiting yet, so push("world") blocks and yields
        chan.push("world");
        trace.push_back("send_done_2");

        chan.close();
    }));

    Fiber::SP receiver(new (std::nothrow) Fiber([&]() noexcept {
        std::string s;
        while (chan.pop(s)) {
            trace.push_back("recv_" + s);
            Fiber::Yield2Ready();
        }
        sc.stop();
    }));

    sc.schedule(receiver);
    sc.schedule(sender);
    sc.start();

    REQUIRE(chan.isClosed());
    REQUIRE(trace == std::vector<std::string>{
        "send_start_1",
        "send_done_1",
        "send_start_2",
        "recv_hello",
        "recv_world",
        "send_done_2"
    });
}
