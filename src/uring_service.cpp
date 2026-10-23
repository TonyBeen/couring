/*************************************************************************
    > File Name: uring_service.cpp
    > Author: hsz
    > Brief: io_uring service and event loop implementation
    > Created Time: 2024年12月22日 星期日 13时29分37秒
 ************************************************************************/

#include "uring_service.h"
#include "uring_operation.h"

#include <liburing.h>
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

#include "logger_internal.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "UringService"

namespace eular {

static thread_local UringService* gUringService = nullptr;

UringService::UringService() noexcept :
    Scheduler("UringService"),
    m_uringHandle(new (std::nothrow) io_uring()),
    m_pendingOps(0),
    m_inited(false),
    m_eventFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
    m_eventFdPollArmed(false)
{
    gUringService = this;
    if (!m_uringHandle) {
        LOGE("Failed to allocate io_uring handle");
        return;
    }

    uint32_t entries = 1024;
    int32_t status = io_uring_queue_init(entries, m_uringHandle.get(), 0);
    if (status != 0) {
        LOGE("io_uring_queue_init error, status=%d: %s", status, strerror(-status));
        return;
    }

    m_inited = true;
    LOGI("UringService initialized successfully with %u entries", entries);
}

UringService::UringService(uint32_t entries, struct io_uring_params *uringParams) noexcept :
    Scheduler("UringService"),
    m_uringHandle(new (std::nothrow) io_uring()),
    m_pendingOps(0),
    m_inited(false),
    m_eventFd(eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)),
    m_eventFdPollArmed(false)
{
    gUringService = this;
    if (!m_uringHandle) {
        LOGE("Failed to allocate io_uring handle");
        return;
    }

    struct io_uring_params params;
    std::memset(&params, 0, sizeof(params));
    if (uringParams == nullptr) {
        uringParams = &params;
    }

    uint32_t ringEntries = entries > 0 ? entries : 1024;
    int32_t status = io_uring_queue_init_params(ringEntries, m_uringHandle.get(), uringParams);
    if (status != 0) {
        LOGE("io_uring_queue_init_params error, status=%d: %s", status, strerror(-status));
        return;
    }

    m_inited = true;
    LOGI("UringService initialized with params, %u entries", ringEntries);
}

UringService::~UringService() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        m_postedTasks.clear();
        m_postedTasks.shrink_to_fit();
    }
    if (m_eventFd >= 0) {
        ::close(m_eventFd);
        m_eventFd = -1;
    }
    if (m_inited && m_uringHandle) {
        io_uring_queue_exit(m_uringHandle.get());
        m_inited = false;
    }
    if (gUringService == this) {
        gUringService = nullptr;
    }
}

UringService* UringService::GetThis() noexcept
{
    return gUringService;
}

struct io_uring_sqe* UringService::getSqe() noexcept
{
    if (!m_inited || !m_uringHandle) {
        return nullptr;
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(m_uringHandle.get());
    if (sqe == nullptr) {
        // SQ is full, flush pending operations to the kernel and retry
        int32_t submitted = io_uring_submit(m_uringHandle.get());
        if (submitted < 0) {
            LOGE("io_uring_submit error during SQ overflow: %d (%s)", submitted, strerror(-submitted));
        }
        sqe = io_uring_get_sqe(m_uringHandle.get());
    }
    return sqe;
}

int32_t UringService::registerBuffers(const struct iovec *iovecs, size_t size) noexcept
{
    if (!m_inited || !m_uringHandle) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }
    return ::io_uring_register_buffers(m_uringHandle.get(), iovecs, size);
}

void UringService::unregisterBuffers() noexcept
{
    if (m_inited && m_uringHandle) {
        (void)::io_uring_unregister_buffers(m_uringHandle.get());
    }
}

int32_t UringService::provideBuffers(const void *buffer, size_t size, int32_t count, int32_t gid, void *user) noexcept
{
    if (!m_inited || !m_uringHandle) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_probe *probe = io_uring_get_probe_ring(m_uringHandle.get());
    if (!probe || !io_uring_opcode_supported(probe, IORING_OP_PROVIDE_BUFFERS)) {
        if (probe) {
            io_uring_free_probe(probe);
        }
        return -static_cast<int32_t>(couring::Errc::OperationNotSupported);
    }
    io_uring_free_probe(probe);

    struct io_uring_sqe *sqe = getSqe();
    if (sqe == nullptr) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    incPendingOps();
    io_uring_prep_provide_buffers(sqe, const_cast<void *>(buffer), size, count, gid, 0);
    sqe->user_data = reinterpret_cast<__u64>(user);
    return 0;
}

int32_t UringService::removeBuffers(int32_t count, int32_t gid) noexcept
{
    if (!m_inited || !m_uringHandle) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = getSqe();
    if (sqe == nullptr) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    incPendingOps();
    io_uring_prep_remove_buffers(sqe, count, gid);
    sqe->user_data = 0;
    return 0;
}

int32_t UringService::cancelOp(uint64_t userData) noexcept
{
    if (!m_inited || !m_uringHandle) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = getSqe();
    if (sqe == nullptr) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    incPendingOps();
    io_uring_prep_cancel64(sqe, userData, 0);
    sqe->user_data = 0;
    io_uring_submit(m_uringHandle.get());
    return 0;
}

void UringService::wakeup() noexcept
{
    if (m_eventFd >= 0) {
        uint64_t val = 1;
        ssize_t n = ::write(m_eventFd, &val, sizeof(val));
        (void)n;
    }
}

void UringService::stop() noexcept
{
    Scheduler::stop();
    wakeup();
}

void UringService::post(std::function<void()> cb) noexcept
{
    if (!cb) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        m_postedTasks.push_back(std::move(cb));
    }
    wakeup();
}

void UringService::drainPostedTasks() noexcept
{
    std::vector<std::function<void()>> posted;
    {
        std::lock_guard<std::mutex> lock(m_postMutex);
        if (!m_postedTasks.empty()) {
            posted.swap(m_postedTasks);
        }
    }
    for (auto& task : posted) {
        schedule(std::move(task));
    }
}

static constexpr uint64_t kEventFdUserData = 0xFEEDBEEF;

void UringService::idle() noexcept
{
    while (!stopping()) {
        // 1. Drain cross-thread posted tasks
        drainPostedTasks();

        // 2. Process expired timers
        m_timerHeap.processExpiredTimers();
        if (stopping()) {
            break;
        }

        // 3. Arm eventfd poll if not armed
        if (m_eventFd >= 0 && !m_eventFdPollArmed) {
            struct io_uring_sqe *eventSqe = getSqe();
            if (eventSqe) {
                incPendingOps();
                io_uring_prep_poll_add(eventSqe, m_eventFd, POLLIN);
                eventSqe->user_data = kEventFdUserData;
                m_eventFdPollArmed = true;
            }
        }

        // 4. Submit pending SQEs
        io_uring_submit(m_uringHandle.get());

        // 5. Calculate nearest timeout
        uint64_t timeoutMs = m_timerHeap.getNearTimeoutMs();
        if (timeoutMs == 0) {
            m_timerHeap.processExpiredTimers();
            Fiber::Yield2Hold();
            continue;
        }

        uint32_t activeOps = m_pendingOps;
        if (m_eventFdPollArmed && activeOps > 0) {
            --activeOps;
        }
        if (activeOps == 0 && timeoutMs == UINT64_MAX && !m_eventFdPollArmed) {
            Fiber::Yield2Hold();
            continue;
        }

        // 6. Wait for completion queue events
        struct io_uring_cqe *cqe = nullptr;
        int32_t ret = 0;

        if (timeoutMs == UINT64_MAX) {
            struct __kernel_timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 50 * 1000000; // 50ms fallback
            ret = io_uring_wait_cqe_timeout(m_uringHandle.get(), &cqe, &ts);
        } else {
            struct __kernel_timespec ts;
            ts.tv_sec = static_cast<int64_t>(timeoutMs / 1000);
            ts.tv_nsec = static_cast<int64_t>((timeoutMs % 1000) * 1000000);
            ret = io_uring_wait_cqe_timeout(m_uringHandle.get(), &cqe, &ts);
        }

        if (ret == -ETIME) {
            m_timerHeap.processExpiredTimers();
            Fiber::Yield2Hold();
            continue;
        }

        if (ret < 0 && ret != -EINTR) {
            Fiber::Yield2Hold();
            continue;
        }

        // 7. Process first cqe from wait
        if (cqe != nullptr) {
            if (m_pendingOps > 0) {
                --m_pendingOps;
            }
            if (io_uring_cqe_get_data(cqe) == reinterpret_cast<void*>(kEventFdUserData)) {
                m_eventFdPollArmed = false;
                uint64_t val = 0;
                ssize_t n = ::read(m_eventFd, &val, sizeof(val));
                (void)n;
                drainPostedTasks();
            } else {
                UringOpBase *op = reinterpret_cast<UringOpBase*>(io_uring_cqe_get_data(cqe));
                if (op) {
                    op->onCompleted(cqe);
                }
            }
            io_uring_cqe_seen(m_uringHandle.get(), cqe);
        }

        // 8. Batch peek any remaining CQEs to drain the completion ring
        struct io_uring_cqe *cqes[32];
        uint32_t count = 0;
        while ((count = io_uring_peek_batch_cqe(m_uringHandle.get(), cqes, 32)) > 0) {
            for (uint32_t i = 0; i < count; ++i) {
                if (m_pendingOps > 0) {
                    --m_pendingOps;
                }
                if (io_uring_cqe_get_data(cqes[i]) == reinterpret_cast<void*>(kEventFdUserData)) {
                    m_eventFdPollArmed = false;
                    uint64_t val = 0;
                    ssize_t n = ::read(m_eventFd, &val, sizeof(val));
                    (void)n;
                    drainPostedTasks();
                } else {
                    UringOpBase *op = reinterpret_cast<UringOpBase*>(io_uring_cqe_get_data(cqes[i]));
                    if (op) {
                        op->onCompleted(cqes[i]);
                    }
                }
            }
            io_uring_cq_advance(m_uringHandle.get(), count);
        }

        // 9. Yield back to scheduler to allow awakened fibers to execute
        Fiber::Yield2Hold();
    }
}

bool UringService::stopping() noexcept
{
    return m_stopping;
}

} // namespace eular
