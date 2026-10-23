/*************************************************************************
    > File Name: io/co_timer.cpp
    > Author: hsz
    > Brief: Coroutine timer sleep implementation
    > Created Time: 2026-10-23
 ************************************************************************/

#include "io/co_timer.h"
#include "uring_service.h"
#include "scheduler.h"
#include "fiber.h"
#include "error_code.h"

namespace couring {

int32_t co_msleep(uint64_t ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    eular::Fiber::SP curr = eular::Fiber::GetThis();
    if (!curr) {
        return -static_cast<int32_t>(couring::Errc::InvalidArgument);
    }

    auto timer = service->timerHeap().addTimer(ms, [curr]() noexcept {
        if (curr && curr->getState() != eular::Fiber::TERM && curr->getState() != eular::Fiber::EXCEPT) {
            eular::Scheduler* sched = eular::Scheduler::GetScheduler();
            if (sched) {
                sched->schedule(curr);
            } else {
                curr->resume();
            }
        }
    });

    if (!timer) {
        return -static_cast<int32_t>(couring::Errc::OutOfMemory);
    }

    eular::Fiber::Yield2Hold();
    return 0;
}

} // namespace couring
