/*************************************************************************
    > File Name: scheduler.h
    > Author: hsz
    > Brief: Single-threaded N:1 coroutine scheduler
    > Created Time: Sun 27 Feb 2022 05:41:28 PM CST
 ************************************************************************/

#ifndef __COURING_SCHEDULER_H__
#define __COURING_SCHEDULER_H__

#include "fiber.h"

#include <cstdint>
#include <memory>
#include <list>
#include <string>
#include <functional>

#include <utils/string8.h>

namespace eular {

class Scheduler
{
public:
    using SP  = std::shared_ptr<Scheduler>;
    using WP  = std::weak_ptr<Scheduler>;
    using Ptr = std::unique_ptr<Scheduler>;

    Scheduler(const eular::String8 &name = "") noexcept;
    virtual ~Scheduler() noexcept;

    const eular::String8 &name() const noexcept { return m_name; }
    static Scheduler* GetScheduler() noexcept;

    void start() noexcept;
    void stop() noexcept;

    template<class FiberOrCb>
    void schedule(FiberOrCb fc) noexcept
    {
        scheduleNoLock(fc);
    }

    template<class Iterator>
    void schedule(Iterator begin, Iterator end) noexcept
    {
        while (begin != end) {
            scheduleNoLock(&*begin);
            ++begin;
        }
    }

private:
    struct FiberTask {
        Fiber::SP fiberPtr;
        std::function<void()> cb;

        FiberTask() noexcept = default;
        FiberTask(Fiber::SP sp) noexcept : fiberPtr(sp) {}
        FiberTask(Fiber::SP *sp) noexcept { fiberPtr.swap(*sp); }
        FiberTask(std::function<void()> f) noexcept : cb(std::move(f)) {}
        FiberTask(std::function<void()> *f) noexcept { cb.swap(*f); }

        void reset() noexcept
        {
            fiberPtr.reset();
            cb = nullptr;
        }
    };

    template<class FiberOrCb>
    void scheduleNoLock(FiberOrCb fc) noexcept {
        try {
            FiberTask ft(fc);
            if (ft.fiberPtr || ft.cb) {
                m_fiberQueue.push_back(std::move(ft));
            }
        } catch (...) {
            // Memory allocation failure in std::list
        }
    }

protected:
    void run() noexcept;
    void setThis() noexcept;

    virtual void idle() noexcept;
    virtual bool stopping() noexcept;

protected:
    bool                        m_stopping;      // Whether stopped
    Fiber::SP                   m_rootFiber;     // Scheduler main execution fiber
    std::list<Fiber::SP>        m_activeFibers;  // Active suspended/running fibers

private:
    eular::String8              m_name;
    std::list<FiberTask>        m_fiberQueue;    // Task queue
};

} // namespace eular

#endif // __COURING_SCHEDULER_H__
