/*************************************************************************
    > File Name: flat_timer_heap.h
    > Author: hsz
    > Brief: Vector-based continuous memory min-heap timer with O(1) lazy deletion
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_FLAT_TIMER_HEAP_H__
#define __COURING_FLAT_TIMER_HEAP_H__

#include <cstdint>
#include <chrono>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <utility>

namespace couring {

class Timer {
public:
    using SP = std::shared_ptr<Timer>;
    using Callback = std::function<void()>;

    Timer(uint64_t id, uint64_t timeout, uint32_t recycle, Callback cb) noexcept :
        m_id(id),
        m_timeout(timeout),
        m_recycle(recycle),
        m_cb(std::move(cb)),
        m_canceled(false)
    {
    }

    uint64_t id() const noexcept { return m_id; }
    uint64_t timeout() const noexcept { return m_timeout; }
    uint32_t recycle() const noexcept { return m_recycle; }
    bool     isCanceled() const noexcept { return m_canceled; }

    void cancel() noexcept { m_canceled = true; }
    void updateTimeout(uint64_t newTimeout) noexcept { m_timeout = newTimeout; }

    void execute() noexcept {
        if (!m_canceled && m_cb) {
            m_cb();
        }
    }

private:
    uint64_t m_id;
    uint64_t m_timeout;
    uint32_t m_recycle;
    Callback m_cb;
    bool     m_canceled;
};

class FlatTimerHeap {
public:
    FlatTimerHeap() noexcept = default;
    ~FlatTimerHeap() noexcept = default;

    static uint64_t CurrentTimeMs() noexcept {
        auto now = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        return static_cast<uint64_t>(ms.count());
    }

    Timer::SP addTimer(uint64_t ms, Timer::Callback cb, uint32_t recycle = 0) noexcept {
        try {
            uint64_t now = CurrentTimeMs();
            uint64_t id = ++s_idCounter;
            Timer::SP timer(new (std::nothrow) Timer(id, now + ms, recycle, std::move(cb)));
            if (!timer) {
                return nullptr;
            }

            m_heap.push_back(timer);
            siftUp(m_heap.size() - 1);
            return timer;
        } catch (...) {
            return nullptr;
        }
    }

    uint64_t getNearTimeoutMs() noexcept {
        cleanCanceledTop();
        if (m_heap.empty()) {
            return UINT64_MAX;
        }

        uint64_t now = CurrentTimeMs();
        uint64_t topTime = m_heap.front()->timeout();
        if (now >= topTime) {
            return 0;
        }
        return topTime - now;
    }

    void processExpiredTimers() noexcept {
        uint64_t now = CurrentTimeMs();
        while (!m_heap.empty()) {
            cleanCanceledTop();
            if (m_heap.empty()) {
                break;
            }

            Timer::SP top = m_heap.front();
            if (top->timeout() > now) {
                break;
            }

            popHeap();

            if (!top->isCanceled()) {
                top->execute();
                if (top->recycle() > 0 && !top->isCanceled()) {
                    top->updateTimeout(now + top->recycle());
                    try {
                        m_heap.push_back(top);
                        siftUp(m_heap.size() - 1);
                    } catch (...) {
                    }
                }
            }
        }
    }

    bool empty() const noexcept {
        return m_heap.empty();
    }

    size_t size() const noexcept {
        return m_heap.size();
    }

private:
    void cleanCanceledTop() noexcept {
        while (!m_heap.empty() && m_heap.front()->isCanceled()) {
            popHeap();
        }
    }

    void siftUp(size_t index) noexcept {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (m_heap[index]->timeout() < m_heap[parent]->timeout()) {
                std::swap(m_heap[index], m_heap[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    void siftDown(size_t index) noexcept {
        size_t n = m_heap.size();
        while (true) {
            size_t smallest = index;
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;

            if (left < n && m_heap[left]->timeout() < m_heap[smallest]->timeout()) {
                smallest = left;
            }
            if (right < n && m_heap[right]->timeout() < m_heap[smallest]->timeout()) {
                smallest = right;
            }

            if (smallest != index) {
                std::swap(m_heap[index], m_heap[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }

    void popHeap() noexcept {
        if (m_heap.empty()) {
            return;
        }
        m_heap[0] = std::move(m_heap.back());
        m_heap.pop_back();
        if (!m_heap.empty()) {
            siftDown(0);
        }
    }

private:
    std::vector<Timer::SP> m_heap;
    static inline std::atomic<uint64_t> s_idCounter{0};
};

} // namespace couring

#endif // __COURING_FLAT_TIMER_HEAP_H__
