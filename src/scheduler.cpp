/*************************************************************************
    > File Name: scheduler.cpp
    > Author: hsz
    > Brief: Single-threaded N:1 coroutine scheduler implementation
    > Created Time: Sun 27 Feb 2022 05:41:32 PM CST
 ************************************************************************/

#include "scheduler.h"

#include <unistd.h>
#include <sys/types.h>

#include <utils/thread.h>
#include "logger_internal.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "scheduler"

namespace eular {

static thread_local Scheduler *gScheduler = nullptr;

Scheduler::Scheduler(const eular::String8 &name) noexcept :
    m_stopping(false),
    m_name(name)
{
    // Ensure the thread main fiber is initialized
    Fiber::GetThis();

    gScheduler = this;
    m_rootFiber.reset(new (std::nothrow) Fiber([this]() noexcept {
        this->run();
    }));

    Thread::SetName(name);
    LOGD("Scheduler created, name=%s, root_thread_id=%ld", name.c_str(), (long)gettid());
}

Scheduler::~Scheduler() noexcept
{
    if (gScheduler == this) {
        gScheduler = nullptr;
    }
}

Scheduler *Scheduler::GetScheduler() noexcept
{
    return gScheduler;
}

void Scheduler::start() noexcept
{
    m_stopping = false;
    if (m_rootFiber && m_rootFiber->getState() != Fiber::TERM) {
        m_rootFiber->resume();
    }
}

void Scheduler::stop() noexcept
{
    m_stopping = true;
}

void Scheduler::run() noexcept
{
    LOGI("Scheduler::run() entered in thread [%s:%ld]", Thread::GetName().c_str(), (long)gettid());
    setThis();

    Fiber::SP idleFiber(new (std::nothrow) Fiber([this]() noexcept {
        this->idle();
    }));
    if (!idleFiber) {
        LOGE("Failed to allocate idle fiber in Scheduler::run()");
        return;
    }

    FiberTask ft;

    while (true) {
        ft.reset();
        bool isActive = false;

        if (!m_fiberQueue.empty()) {
            auto it = m_fiberQueue.begin();
            ft = std::move(*it);
            m_fiberQueue.erase(it);
            isActive = true;
        }

        if (ft.fiberPtr) {
            Fiber::SP curFiber = std::move(ft.fiberPtr);
            ft.reset();

            if (curFiber->getState() != Fiber::EXEC && curFiber->getState() != Fiber::TERM && curFiber->getState() != Fiber::EXCEPT) {
                curFiber->resume();
                if (curFiber->getState() == Fiber::READY) {
                    schedule(curFiber);
                } else if (curFiber->getState() == Fiber::HOLD) {
                    bool found = false;
                    for (const auto& f : m_activeFibers) {
                        if (f == curFiber) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        m_activeFibers.push_back(curFiber);
                    }
                }
            }

            if (curFiber->getState() == Fiber::TERM || curFiber->getState() == Fiber::EXCEPT) {
                for (auto it = m_activeFibers.begin(); it != m_activeFibers.end(); ++it) {
                    if (*it == curFiber) {
                        m_activeFibers.erase(it);
                        break;
                    }
                }
            }
        } else if (ft.cb) {
            Fiber::SP cbFiber(new (std::nothrow) Fiber(std::move(ft.cb)));
            ft.reset();

            if (cbFiber) {
                cbFiber->resume();
                if (cbFiber->getState() == Fiber::READY) {
                    schedule(cbFiber);
                } else if (cbFiber->getState() == Fiber::HOLD) {
                    m_activeFibers.push_back(cbFiber);
                }
            }
        } else {
            // Clean up any fibers in m_activeFibers that have terminated
            for (auto it = m_activeFibers.begin(); it != m_activeFibers.end(); ) {
                if ((*it)->getState() == Fiber::TERM || (*it)->getState() == Fiber::EXCEPT) {
                    it = m_activeFibers.erase(it);
                } else {
                    ++it;
                }
            }

            if (isActive) {
                continue;
            }

            if (stopping()) {
                LOGI("Scheduler stopping condition met, exiting run loop");
                break;
            }

            idleFiber->resume();
            if (idleFiber->getState() == Fiber::TERM || idleFiber->getState() == Fiber::EXCEPT) {
                LOGI("Idle fiber exited, terminating scheduler loop");
                break;
            }
        }
    }

    m_activeFibers.clear();
    LOGI("Scheduler::run() exited");
}

void Scheduler::setThis() noexcept
{
    gScheduler = this;
}

void Scheduler::idle() noexcept
{
    while (!stopping()) {
        Fiber::Yield2Hold();
    }
}

bool Scheduler::stopping() noexcept
{
    return m_stopping;
}

} // namespace eular
