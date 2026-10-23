/*************************************************************************
    > File Name: sync/co_wait_group.h
    > Author: hsz
    > Brief: Single-threaded cooperative coroutine WaitGroup
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_SYNC_CO_WAIT_GROUP_H__
#define __COURING_SYNC_CO_WAIT_GROUP_H__

#include <cstdint>
#include <vector>
#include "fiber.h"
#include "scheduler.h"

namespace couring {

class CoWaitGroup {
public:
    CoWaitGroup() noexcept : m_count(0) {}
    ~CoWaitGroup() noexcept = default;

    CoWaitGroup(const CoWaitGroup&) = delete;
    CoWaitGroup& operator=(const CoWaitGroup&) = delete;

    void add(int32_t delta = 1) noexcept {
        m_count += delta;
        if (m_count <= 0) {
            m_count = 0;
            wakeupAll();
        }
    }

    void done() noexcept {
        add(-1);
    }

    void wait() noexcept {
        if (m_count <= 0) {
            return;
        }
        eular::Fiber::SP curr = eular::Fiber::GetThis();
        if (curr) {
            m_waiters.push_back(curr);
            eular::Fiber::Yield2Hold();
        }
    }

    int32_t count() const noexcept { return m_count; }

private:
    void wakeupAll() noexcept {
        std::vector<eular::Fiber::SP> waiters;
        waiters.swap(m_waiters);
        eular::Scheduler* sched = eular::Scheduler::GetScheduler();
        for (auto& fiber : waiters) {
            if (fiber && fiber->getState() != eular::Fiber::TERM && fiber->getState() != eular::Fiber::EXCEPT) {
                if (sched) {
                    sched->schedule(fiber);
                } else {
                    fiber->resume();
                }
            }
        }
    }

private:
    int32_t                       m_count;
    std::vector<eular::Fiber::SP> m_waiters;
};

} // namespace couring

#endif // __COURING_SYNC_CO_WAIT_GROUP_H__
