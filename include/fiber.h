/*************************************************************************
    > File Name: fiber.h
    > Author: hsz
    > Brief: Stackful coroutine implementation based on fcontext
    > Created Time: Wed 22 Sep 2021 04:17:18 PM CST
 ************************************************************************/

#ifndef __COURING_FIBER_H__
#define __COURING_FIBER_H__

#include <cstdint>
#include <cstddef>
#include <functional>
#include <memory>

#include <fcontext/fcontext.h>

namespace eular {

class Scheduler;

/**
 * @brief Coroutine (Fiber) class
 */
class Fiber : public std::enable_shared_from_this<Fiber>
{
public:
    using SP  = std::shared_ptr<Fiber>;
    using WP  = std::weak_ptr<Fiber>;
    using Ptr = std::unique_ptr<Fiber>;

    enum FiberState : int32_t {
        READY,      // Ready to execute
        HOLD,       // Suspended (waiting for IO / timer)
        EXEC,       // Currently executing
        TERM,       // Terminated
        EXCEPT      // Exceptional state
    };

    Fiber(std::function<void()> cb, uint64_t stackSize = 0) noexcept;
    ~Fiber() noexcept;

    void reset(std::function<void()> cb) noexcept;

    static void      SetThis(Fiber *f) noexcept;
    static Fiber::SP GetThis() noexcept;

    void call() noexcept;
    void back() noexcept;
    void resume() noexcept;

    static void Yield2Hold() noexcept;
    static void Yield2Ready() noexcept;

    // Backward-compatibility aliases for legacy spelling
    static void Yeild2Hold() noexcept { Yield2Hold(); }
    static void Yeild2Ready() noexcept { Yield2Ready(); }

    FiberState  getState() const noexcept { return m_state; }
    uint64_t    getFiberId() const noexcept { return m_fiberId; }
    static uint64_t GetFiberID() noexcept;

    bool isTerminated() const noexcept { return m_state == TERM || m_state == EXCEPT; }

private:
    Fiber() noexcept;                    // Constructor for thread main fiber
    static void FiberEntry(transfer_t t); // Entry function for fcontext
    void swapIn() noexcept;               // Switch to foreground
    void swapOut() noexcept;              // Switch to background

private:
    fcontext_t            m_ctx;
    fcontext_t            m_callerCtx;
    FiberState            m_state;
    uint64_t              m_fiberId;
    uint64_t              m_stackSize;
    void*                 m_stack;         // mmap base address (including guard page)
    std::function<void()> m_cb;

    friend class Scheduler;
};

} // namespace eular

#endif // __COURING_FIBER_H__
