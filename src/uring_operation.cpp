/*************************************************************************
    > File Name: uring_operation.cpp
    > Author: hsz
    > Brief: Operation implementation for io_uring
    > Created Time: 2024年12月22日 星期日 13时35分24秒
 ************************************************************************/

#include "uring_operation.h"
#include "scheduler.h"

#include <liburing.h>
#include "logger_internal.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "uring_op"

namespace eular {

void UringOpSync::onCompleted(struct io_uring_cqe *cqe) noexcept
{
    if (cqe) {
        m_res = cqe->res;
        m_flags = cqe->flags;
    } else {
        m_res = -1;
        m_flags = 0;
    }
    m_completed = true;

    LOGD("UringOpSync::onCompleted: res=%d, fiber_id=%lu", m_res, m_fiber ? m_fiber->getFiberId() : 0);

    if (m_fiber && m_fiber->getState() != Fiber::TERM && m_fiber->getState() != Fiber::EXCEPT) {
        Scheduler* sched = Scheduler::GetScheduler();
        if (sched) {
            sched->schedule(m_fiber);
        } else {
            m_fiber->resume();
        }
    }
}

void UringOpCallback::onCompleted(struct io_uring_cqe *cqe) noexcept
{
    if (m_cb && cqe) {
        m_cb(cqe->res, cqe->flags);
    }
}

} // namespace eular
