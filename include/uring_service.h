/*************************************************************************
    > File Name: uring_service.h
    > Author: hsz
    > Brief: io_uring service and event loop for coroutines
    > Created Time: 2024年12月22日 星期日 13时29分33秒
 ************************************************************************/

#ifndef __COURING_URING_SERVICE_H__
#define __COURING_URING_SERVICE_H__

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <mutex>
#include <functional>

#include "scheduler.h"
#include "flat_timer_heap.h"
#include "error_code.h"

struct io_uring;
struct io_uring_sqe;
struct io_uring_params;
struct iovec;

namespace eular {

class UringService : public Scheduler
{
public:
    using SP  = std::shared_ptr<UringService>;
    using WP  = std::weak_ptr<UringService>;
    using Ptr = std::unique_ptr<UringService>;

    UringService() noexcept;
    UringService(uint32_t entries, struct io_uring_params *uringParams = nullptr) noexcept;
    ~UringService() noexcept override;

    static UringService* GetThis() noexcept;

    bool isValid() const noexcept { return m_inited; }
    struct io_uring* handle() const noexcept { return m_uringHandle.get(); }

    // Safe retrieval of SQE with auto-submit retry on queue full
    struct io_uring_sqe* getSqe() noexcept;

    // Direct buffer registration
    int32_t registerBuffers(const struct iovec *iovecs, size_t size) noexcept;
    void    unregisterBuffers() noexcept;

    // Buffer provide
    int32_t provideBuffers(const void *buffer, size_t size, int32_t count, int32_t gid, void *user) noexcept;
    int32_t removeBuffers(int32_t count, int32_t gid) noexcept;

    // Async cancel by user_data
    int32_t cancelOp(uint64_t userData) noexcept;

    // Cross-thread task dispatch and instant wakeup
    void post(std::function<void()> cb) noexcept;
    void wakeup() noexcept;
    void stop() noexcept;

    couring::FlatTimerHeap& timerHeap() noexcept { return m_timerHeap; }

    void incPendingOps() noexcept { ++m_pendingOps; }
    uint32_t pendingOps() const noexcept { return m_pendingOps; }

protected:
    void idle() noexcept override;
    bool stopping() noexcept override;

private:
    void drainPostedTasks() noexcept;

private:
    std::unique_ptr<struct io_uring>   m_uringHandle;
    couring::FlatTimerHeap             m_timerHeap;
    uint32_t                           m_pendingOps;
    bool                               m_inited;
    int32_t                            m_eventFd;
    bool                               m_eventFdPollArmed;
    std::mutex                         m_postMutex;
    std::vector<std::function<void()>> m_postedTasks;
};

} // namespace eular

#endif // __COURING_URING_SERVICE_H__
