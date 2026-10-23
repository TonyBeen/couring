/*************************************************************************
    > File Name: sync/co_semaphore.h
    > Author: hsz
    > Brief: Single-threaded cooperative coroutine Semaphore
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_SYNC_CO_SEMAPHORE_H__
#define __COURING_SYNC_CO_SEMAPHORE_H__

#include <cstdint>
#include <deque>
#include <utility>
#include "fiber.h"
#include "scheduler.h"

namespace couring {

class CoSemaphore {
public:
    explicit CoSemaphore(int32_t initial_count = 0) noexcept
        : m_count(initial_count) {}
    ~CoSemaphore() noexcept = default;

    CoSemaphore(const CoSemaphore&) = delete;
    CoSemaphore& operator=(const CoSemaphore&) = delete;

    void acquire(int32_t count = 1) noexcept {
        if (count <= 0) {
            return;
        }
        if (m_count >= count && m_waiters.empty()) {
            m_count -= count;
            return;
        }
        eular::Fiber::SP curr = eular::Fiber::GetThis();
        if (!curr) {
            return;
        }
        m_waiters.push_back({curr, count});
        eular::Fiber::Yield2Hold();
    }

    bool tryAcquire(int32_t count = 1) noexcept {
        if (count <= 0) {
            return true;
        }
        if (m_count >= count && m_waiters.empty()) {
            m_count -= count;
            return true;
        }
        return false;
    }

    void release(int32_t count = 1) noexcept {
        if (count <= 0) {
            return;
        }
        m_count += count;
        wakeupWaiters();
    }

    int32_t available() const noexcept { return m_count; }

private:
    void wakeupWaiters() noexcept {
        eular::Scheduler* sched = eular::Scheduler::GetScheduler();
        while (!m_waiters.empty() && m_count >= m_waiters.front().second) {
            auto waiter = m_waiters.front();
            m_waiters.pop_front();
            m_count -= waiter.second;
            if (waiter.first && waiter.first->getState() != eular::Fiber::TERM &&
                waiter.first->getState() != eular::Fiber::EXCEPT) {
                if (sched) {
                    sched->schedule(waiter.first);
                } else {
                    waiter.first->resume();
                }
            }
        }
    }

private:
    int32_t                                          m_count;
    std::deque<std::pair<eular::Fiber::SP, int32_t>> m_waiters;
};

} // namespace couring

#endif // __COURING_SYNC_CO_SEMAPHORE_H__
