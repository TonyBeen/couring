/*************************************************************************
    > File Name: sync/co_channel.h
    > Author: hsz
    > Brief: Single-threaded cooperative coroutine Channel
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_SYNC_CO_CHANNEL_H__
#define __COURING_SYNC_CO_CHANNEL_H__

#include <cstddef>
#include <deque>
#include <utility>
#include "fiber.h"
#include "scheduler.h"

namespace couring {

template <typename T>
class CoChannel {
public:
    explicit CoChannel(size_t capacity = 0) noexcept
        : m_capacity(capacity), m_closed(false) {}

    ~CoChannel() noexcept {
        close();
    }

    CoChannel(const CoChannel&) = delete;
    CoChannel& operator=(const CoChannel&) = delete;

    bool push(T val) noexcept {
        if (m_closed) {
            return false;
        }

        // If a receiver is waiting, hand over directly
        if (!m_recvWaiters.empty()) {
            RecvWaiter* r = m_recvWaiters.front();
            m_recvWaiters.pop_front();
            if (r->dest) {
                *(r->dest) = std::move(val);
            }
            r->ok = true;
            scheduleFiber(r->fiber);
            return true;
        }

        // If buffer space is available, push into buffer
        if (m_capacity > 0 && m_queue.size() < m_capacity) {
            m_queue.push_back(std::move(val));
            return true;
        }

        // Wait until receiver consumes or channel closes
        eular::Fiber::SP curr = eular::Fiber::GetThis();
        if (!curr) {
            return false;
        }

        SendWaiter waiter{curr, &val, false};
        m_sendWaiters.push_back(&waiter);
        eular::Fiber::Yield2Hold();

        return waiter.done;
    }

    bool tryPush(T val) noexcept {
        if (m_closed) {
            return false;
        }
        if (!m_recvWaiters.empty()) {
            RecvWaiter* r = m_recvWaiters.front();
            m_recvWaiters.pop_front();
            if (r->dest) {
                *(r->dest) = std::move(val);
            }
            r->ok = true;
            scheduleFiber(r->fiber);
            return true;
        }
        if (m_capacity > 0 && m_queue.size() < m_capacity) {
            m_queue.push_back(std::move(val));
            return true;
        }
        return false;
    }

    bool pop(T& val) noexcept {
        // 1. If buffered elements exist
        if (!m_queue.empty()) {
            val = std::move(m_queue.front());
            m_queue.pop_front();

            // If a sender is blocked waiting for room, move sender's item into queue
            if (!m_sendWaiters.empty()) {
                SendWaiter* s = m_sendWaiters.front();
                m_sendWaiters.pop_front();
                if (s->data) {
                    m_queue.push_back(std::move(*(s->data)));
                }
                s->done = true;
                scheduleFiber(s->fiber);
            }
            return true;
        }

        // 2. If unbuffered (capacity == 0) and a sender is waiting
        if (!m_sendWaiters.empty()) {
            SendWaiter* s = m_sendWaiters.front();
            m_sendWaiters.pop_front();
            if (s->data) {
                val = std::move(*(s->data));
            }
            s->done = true;
            scheduleFiber(s->fiber);
            return true;
        }

        // 3. Queue empty, channel closed
        if (m_closed) {
            return false;
        }

        // 4. Must wait for sender
        eular::Fiber::SP curr = eular::Fiber::GetThis();
        if (!curr) {
            return false;
        }

        RecvWaiter waiter{curr, &val, false};
        m_recvWaiters.push_back(&waiter);
        eular::Fiber::Yield2Hold();

        return waiter.ok;
    }

    bool tryPop(T& val) noexcept {
        if (!m_queue.empty()) {
            val = std::move(m_queue.front());
            m_queue.pop_front();
            if (!m_sendWaiters.empty()) {
                SendWaiter* s = m_sendWaiters.front();
                m_sendWaiters.pop_front();
                if (s->data) {
                    m_queue.push_back(std::move(*(s->data)));
                }
                s->done = true;
                scheduleFiber(s->fiber);
            }
            return true;
        }
        if (!m_sendWaiters.empty()) {
            SendWaiter* s = m_sendWaiters.front();
            m_sendWaiters.pop_front();
            if (s->data) {
                val = std::move(*(s->data));
            }
            s->done = true;
            scheduleFiber(s->fiber);
            return true;
        }
        return false;
    }

    void close() noexcept {
        if (m_closed) {
            return;
        }
        m_closed = true;

        while (!m_sendWaiters.empty()) {
            SendWaiter* s = m_sendWaiters.front();
            m_sendWaiters.pop_front();
            s->done = false;
            scheduleFiber(s->fiber);
        }

        while (!m_recvWaiters.empty()) {
            RecvWaiter* r = m_recvWaiters.front();
            m_recvWaiters.pop_front();
            r->ok = false;
            scheduleFiber(r->fiber);
        }
    }

    bool isClosed() const noexcept { return m_closed; }
    size_t size() const noexcept { return m_queue.size(); }
    bool empty() const noexcept { return m_queue.empty(); }
    size_t capacity() const noexcept { return m_capacity; }

private:
    struct SendWaiter {
        eular::Fiber::SP fiber;
        T*               data{nullptr};
        bool             done{false};
    };

    struct RecvWaiter {
        eular::Fiber::SP fiber;
        T*               dest{nullptr};
        bool             ok{false};
    };

    static void scheduleFiber(eular::Fiber::SP fiber) noexcept {
        if (!fiber) {
            return;
        }
        if (fiber->getState() == eular::Fiber::TERM || fiber->getState() == eular::Fiber::EXCEPT) {
            return;
        }
        eular::Scheduler* sched = eular::Scheduler::GetScheduler();
        if (sched) {
            sched->schedule(fiber);
        } else {
            fiber->resume();
        }
    }

private:
    size_t                  m_capacity;
    bool                    m_closed;
    std::deque<T>           m_queue;
    std::deque<SendWaiter*> m_sendWaiters;
    std::deque<RecvWaiter*> m_recvWaiters;
};

} // namespace couring

#endif // __COURING_SYNC_CO_CHANNEL_H__
