/*************************************************************************
    > File Name: io/co_file.cpp
    > Author: hsz
    > Brief: Coroutine asynchronous File I/O implementation based on io_uring
    > Created Time: 2026-10-23
 ************************************************************************/

#include "io/co_file.h"
#include "buffer/buffer_id.h"
#include "uring_service.h"
#include "uring_operation.h"
#include "fiber.h"
#include "error_code.h"

#include <liburing.h>

namespace couring {

ssize_t co_read_file(int32_t fd, void *buf, size_t len, int64_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = service->getSqe();
    if (!sqe) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    eular::UringOpSync op(eular::Fiber::GetThis());
    service->incPendingOps();
    io_uring_prep_read(sqe, fd, buf, len, static_cast<__u64>(offset));
    sqe->user_data = reinterpret_cast<__u64>(&op);

    couring::Timer::SP timer = nullptr;
    if (timeout_ms > 0) {
        timer = service->timerHeap().addTimer(timeout_ms, [&op, service]() noexcept {
            op.setTimedOut(true);
            service->cancelOp(reinterpret_cast<__u64>(&op));
        });
    }

    eular::Fiber::Yield2Hold();

    if (timer) {
        timer->cancel();
    }

    return static_cast<ssize_t>(op.result());
}

ssize_t co_write_file(int32_t fd, const void *buf, size_t len, int64_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = service->getSqe();
    if (!sqe) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    eular::UringOpSync op(eular::Fiber::GetThis());
    service->incPendingOps();
    io_uring_prep_write(sqe, fd, buf, len, static_cast<__u64>(offset));
    sqe->user_data = reinterpret_cast<__u64>(&op);

    couring::Timer::SP timer = nullptr;
    if (timeout_ms > 0) {
        timer = service->timerHeap().addTimer(timeout_ms, [&op, service]() noexcept {
            op.setTimedOut(true);
            service->cancelOp(reinterpret_cast<__u64>(&op));
        });
    }

    eular::Fiber::Yield2Hold();

    if (timer) {
        timer->cancel();
    }

    return static_cast<ssize_t>(op.result());
}

ssize_t co_read_fixed(int32_t fd, void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = service->getSqe();
    if (!sqe) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    eular::UringOpSync op(eular::Fiber::GetThis());
    service->incPendingOps();
    io_uring_prep_read_fixed(sqe, fd, buf, static_cast<unsigned>(len), static_cast<__u64>(offset), buf_index);
    sqe->user_data = reinterpret_cast<__u64>(&op);

    couring::Timer::SP timer = nullptr;
    if (timeout_ms > 0) {
        timer = service->timerHeap().addTimer(timeout_ms, [&op, service]() noexcept {
            op.setTimedOut(true);
            service->cancelOp(reinterpret_cast<__u64>(&op));
        });
    }

    eular::Fiber::Yield2Hold();

    if (timer) {
        timer->cancel();
    }

    return static_cast<ssize_t>(op.result());
}

ssize_t co_write_fixed(int32_t fd, const void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    struct io_uring_sqe *sqe = service->getSqe();
    if (!sqe) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    eular::UringOpSync op(eular::Fiber::GetThis());
    service->incPendingOps();
    io_uring_prep_write_fixed(sqe, fd, buf, static_cast<unsigned>(len), static_cast<__u64>(offset), buf_index);
    sqe->user_data = reinterpret_cast<__u64>(&op);

    couring::Timer::SP timer = nullptr;
    if (timeout_ms > 0) {
        timer = service->timerHeap().addTimer(timeout_ms, [&op, service]() noexcept {
            op.setTimedOut(true);
            service->cancelOp(reinterpret_cast<__u64>(&op));
        });
    }

    eular::Fiber::Yield2Hold();

    if (timer) {
        timer->cancel();
    }

    return static_cast<ssize_t>(op.result());
}

ssize_t co_read_fixed(int32_t fd, BufferId *bid, size_t len, int64_t offset, uint64_t timeout_ms) noexcept
{
    if (!bid || !bid->buffer()) {
        return -EINVAL;
    }
    return co_read_fixed(fd, bid->buffer(), len, offset, bid->bid(), timeout_ms);
}

ssize_t co_write_fixed(int32_t fd, const BufferId *bid, size_t len, int64_t offset, uint64_t timeout_ms) noexcept
{
    if (!bid || !bid->buffer()) {
        return -EINVAL;
    }
    return co_write_fixed(fd, bid->buffer(), len, offset, bid->bid(), timeout_ms);
}

} // namespace couring
