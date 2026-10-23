/*************************************************************************
    > File Name: fiber.cpp
    > Author: hsz
    > Brief: Stackful coroutine implementation based on fcontext
    > Created Time: Wed 22 Sep 2021 04:17:18 PM CST
 ************************************************************************/

#include "fiber.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <atomic>
#include <exception>
#include <cassert>

#include "logger_internal.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "fiber"

namespace eular {

static std::atomic<uint64_t> gFiberId{0};
static std::atomic<uint64_t> gFiberCount{0};

static thread_local Fiber*    gCurrentFiber = nullptr;
static thread_local Fiber::SP gThreadMainFiber = nullptr;

static constexpr size_t kPageSize = 4096;
static constexpr size_t kDefaultStackSize = 16 * 1024; // 16 KB

static uint64_t getStackSize() noexcept
{
    return kDefaultStackSize;
}

// Private constructor for thread's root/main fiber
Fiber::Fiber() noexcept :
    m_ctx(nullptr),
    m_callerCtx(nullptr),
    m_state(EXEC),
    m_fiberId(++gFiberId),
    m_stackSize(0),
    m_stack(nullptr),
    m_cb(nullptr)
{
    ++gFiberCount;
    SetThis(this);
    LOGD("Main Fiber created, id=%lu, total=%lu", m_fiberId, gFiberCount.load());
}

Fiber::Fiber(std::function<void()> cb, uint64_t stackSize) noexcept :
    m_ctx(nullptr),
    m_callerCtx(nullptr),
    m_state(READY),
    m_fiberId(++gFiberId),
    m_stackSize(0),
    m_stack(nullptr),
    m_cb(std::move(cb))
{
    ++gFiberCount;

    uint64_t requestedSize = stackSize ? stackSize : getStackSize();
    // Align stack size to page size
    m_stackSize = (requestedSize + kPageSize - 1) & ~(kPageSize - 1);

    size_t totalAllocSize = m_stackSize + kPageSize; // Extra page for bottom guard page
    void* mem = mmap(nullptr, totalAllocSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        LOGE("Failed to mmap stack for fiber %lu, errno=%d: %s", m_fiberId, errno, strerror(errno));
        m_state = EXCEPT;
        m_stack = nullptr;
        return;
    }

    // Set bottom page as guard page (PROT_NONE) to detect stack overflow
    if (mprotect(mem, kPageSize, PROT_NONE) != 0) {
        LOGE("Failed to mprotect guard page for fiber %lu, errno=%d: %s", m_fiberId, errno, strerror(errno));
        munmap(mem, totalAllocSize);
        m_state = EXCEPT;
        m_stack = nullptr;
        return;
    }

    m_stack = mem;

    // Stack grows downward. Top of stack is at (base + totalAllocSize)
    uint8_t* stackTop = static_cast<uint8_t*>(m_stack) + totalAllocSize;
    // Ensure 16-byte alignment
    stackTop = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(stackTop) & ~0xFull);

    m_ctx = make_fcontext(stackTop, m_stackSize, &Fiber::FiberEntry);
    LOGD("Fiber created, id=%lu, stack_size=%lu, total=%lu", m_fiberId, m_stackSize, gFiberCount.load());
}

Fiber::~Fiber() noexcept
{
    --gFiberCount;
    LOGD("Fiber destroyed, id=%lu, total=%lu", m_fiberId, gFiberCount.load());

    if (m_stack) {
        assert(gCurrentFiber != this && "Fiber cannot be destroyed while running on its own stack");
        // Free mmap stack including guard page
        size_t totalAllocSize = m_stackSize + kPageSize;
        munmap(m_stack, totalAllocSize);
        m_stack = nullptr;
    } else {
        // Main fiber
        if (gCurrentFiber == this) {
            SetThis(nullptr);
        }
    }
}

void Fiber::reset(std::function<void()> cb) noexcept
{
    assert(m_stack != nullptr && "Main fiber cannot be reset");
    assert((m_state == TERM || m_state == EXCEPT || m_state == READY) && "Cannot reset active fiber");

    m_cb = std::move(cb);
    size_t totalAllocSize = m_stackSize + kPageSize;
    uint8_t* stackTop = static_cast<uint8_t*>(m_stack) + totalAllocSize;
    stackTop = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(stackTop) & ~0xFull);

    m_ctx = make_fcontext(stackTop, m_stackSize, &Fiber::FiberEntry);
    m_callerCtx = nullptr;
    m_state = READY;
}

void Fiber::swapIn() noexcept
{
    SetThis(this);
    assert(m_state != EXEC && "Fiber state cannot be EXEC when swapped in");
    assert(m_ctx != nullptr && "m_ctx must not be null in swapIn");
    m_state = EXEC;

    int savedErrno = errno;
    transfer_t t = jump_fcontext(m_ctx, this);
    m_ctx = t.fctx;
    errno = savedErrno;
}

void Fiber::swapOut() noexcept
{
    SetThis(gThreadMainFiber.get());
    assert(m_callerCtx != nullptr && "m_callerCtx must not be null in swapOut");

    int savedErrno = errno;
    transfer_t t = jump_fcontext(m_callerCtx, nullptr);
    m_callerCtx = t.fctx;
    errno = savedErrno;
}

void Fiber::SetThis(Fiber *f) noexcept
{
    gCurrentFiber = f;
}

Fiber::SP Fiber::GetThis() noexcept
{
    if (gCurrentFiber) {
        return gCurrentFiber->shared_from_this();
    }

    Fiber* raw = new (std::nothrow) Fiber();
    if (!raw) {
        LOGE("Failed to allocate main fiber");
        return nullptr;
    }

    Fiber::SP fiber(raw);
    gThreadMainFiber = fiber;
    return gCurrentFiber->shared_from_this();
}

void Fiber::call() noexcept
{
    swapIn();
}

void Fiber::back() noexcept
{
    swapOut();
}

void Fiber::resume() noexcept
{
    swapIn();
}

void Fiber::Yield2Hold() noexcept
{
    Fiber::SP ptr = GetThis();
    if (ptr) {
        assert(ptr->m_state == EXEC && "Current fiber must be EXEC to yield");
        ptr->m_state = HOLD;
        ptr->swapOut();
    }
}

void Fiber::Yield2Ready() noexcept
{
    Fiber::SP ptr = GetThis();
    if (ptr) {
        assert(ptr->m_state == EXEC && "Current fiber must be EXEC to yield");
        ptr->m_state = READY;
        ptr->swapOut();
    }
}

uint64_t Fiber::GetFiberID() noexcept
{
    if (gCurrentFiber) {
        return gCurrentFiber->m_fiberId;
    }
    return 0;
}

void Fiber::FiberEntry(transfer_t t)
{
    Fiber* curr = static_cast<Fiber*>(t.data);
    assert(curr != nullptr && "FiberEntry received null fiber pointer");

    curr->m_callerCtx = t.fctx;

    try {
        if (curr->m_cb) {
            curr->m_cb();
        }
        curr->m_state = TERM;
    } catch (const std::exception& e) {
        curr->m_state = EXCEPT;
        LOGE("Fiber caught std::exception: %s, id=%lu", e.what(), curr->m_fiberId);
    } catch (...) {
        curr->m_state = EXCEPT;
        LOGE("Fiber caught unknown exception, id=%lu", curr->m_fiberId);
    }

    // Explicitly release callback to break lambda capture cycles
    curr->m_cb = nullptr;

    Fiber* raw = curr;
    curr = nullptr;

    SetThis(gThreadMainFiber.get());
    assert(raw->m_callerCtx != nullptr && "raw->m_callerCtx must not be null at fiber exit");
    jump_fcontext(raw->m_callerCtx, nullptr);

    assert(false && "Fiber reached unreachable code after termination");
}

} // namespace eular
