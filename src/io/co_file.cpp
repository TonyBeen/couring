/*************************************************************************
    > File Name: io/co_file.cpp
    > Author: hsz
    > Brief: Coroutine asynchronous File I/O implementation based on io_uring
    > Created Time: 2026-10-23
 ************************************************************************/

#include "io/co_file.h"
#include "io/co_socket.h"
#include "buffer/buffer_id.h"
#include "uring_service.h"
#include "uring_operation.h"
#include "fiber.h"
#include "error_code.h"

#include <cerrno>
#include <liburing.h>

namespace couring {

namespace {

template <typename PrepFunc>
inline int32_t executeOpWithTimeout(eular::UringService* service, uint64_t timeout_ms, PrepFunc&& prep) noexcept
{
    struct io_uring_sqe *sqe = service->getSqe();
    if (!sqe) {
        return -static_cast<int32_t>(couring::Errc::SubmissionQueueFull);
    }

    eular::UringOpSync op(eular::Fiber::GetThis());
    service->incPendingOps();
    prep(sqe);
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

    return op.result();
}

} // namespace

int co_openat(int32_t dfd, const char *path, int flags, mode_t mode, uint64_t timeout_ms) noexcept
{
    if (!path) {
        return -EINVAL;
    }
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [dfd, path, flags, mode](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_openat(sqe, dfd, path, flags, mode);
    });
}

int co_open(const char *path, int flags, mode_t mode, uint64_t timeout_ms) noexcept
{
    return co_openat(AT_FDCWD, path, flags, mode, timeout_ms);
}

ssize_t co_pread(int32_t fd, void *buf, size_t count, off_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, count, offset](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_read(sqe, fd, buf, static_cast<unsigned>(count), static_cast<__u64>(offset));
    });
}

ssize_t co_pwrite(int32_t fd, const void *buf, size_t count, off_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, count, offset](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_write(sqe, fd, buf, static_cast<unsigned>(count), static_cast<__u64>(offset));
    });
}

ssize_t co_preadv(int32_t fd, const struct iovec *iov, int32_t iovcnt, off_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, iov, iovcnt, offset](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_readv(sqe, fd, iov, static_cast<unsigned>(iovcnt), static_cast<__u64>(offset));
    });
}

ssize_t co_pwritev(int32_t fd, const struct iovec *iov, int32_t iovcnt, off_t offset, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, iov, iovcnt, offset](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_writev(sqe, fd, iov, static_cast<unsigned>(iovcnt), static_cast<__u64>(offset));
    });
}

ssize_t co_readv(int32_t fd, const struct iovec *iov, int32_t iovcnt, uint64_t timeout_ms) noexcept
{
    return co_preadv(fd, iov, iovcnt, static_cast<off_t>(-1), timeout_ms);
}

ssize_t co_writev(int32_t fd, const struct iovec *iov, int32_t iovcnt, uint64_t timeout_ms) noexcept
{
    return co_pwritev(fd, iov, iovcnt, static_cast<off_t>(-1), timeout_ms);
}

ssize_t co_pread_exact(int32_t fd, void *buf, size_t len, off_t offset, uint64_t timeout_ms) noexcept
{
    size_t total = 0;
    char *ptr = static_cast<char *>(buf);
    while (total < len) {
        ssize_t n = co_pread(fd, ptr + total, len - total, offset + static_cast<off_t>(total), timeout_ms);
        if (n == 0) {
            break; // EOF
        }
        if (n < 0) {
            return total > 0 ? static_cast<ssize_t>(total) : n;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

ssize_t co_pwrite_all(int32_t fd, const void *buf, size_t len, off_t offset, uint64_t timeout_ms) noexcept
{
    size_t total = 0;
    const char *ptr = static_cast<const char *>(buf);
    while (total < len) {
        ssize_t n = co_pwrite(fd, ptr + total, len - total, offset + static_cast<off_t>(total), timeout_ms);
        if (n <= 0) {
            return total > 0 ? static_cast<ssize_t>(total) : n;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

ssize_t co_read_exact(int32_t fd, void *buf, size_t len, uint64_t timeout_ms) noexcept
{
    size_t total = 0;
    char *ptr = static_cast<char *>(buf);
    while (total < len) {
        ssize_t n = co_read(fd, ptr + total, len - total, timeout_ms);
        if (n == 0) {
            break; // EOF
        }
        if (n < 0) {
            return total > 0 ? static_cast<ssize_t>(total) : n;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

ssize_t co_write_all(int32_t fd, const void *buf, size_t len, uint64_t timeout_ms) noexcept
{
    size_t total = 0;
    const char *ptr = static_cast<const char *>(buf);
    while (total < len) {
        ssize_t n = co_write(fd, ptr + total, len - total, timeout_ms);
        if (n <= 0) {
            return total > 0 ? static_cast<ssize_t>(total) : n;
        }
        total += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(total);
}

int co_fallocate(int32_t fd, int32_t mode, off_t offset, off_t len, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, mode, offset, len](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_fallocate(sqe, fd, mode, static_cast<__u64>(offset), static_cast<__u64>(len));
    });
}

int co_fsync(int32_t fd, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_fsync(sqe, fd, 0);
    });
}

int co_fdatasync(int32_t fd, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_fsync(sqe, fd, IORING_FSYNC_DATASYNC);
    });
}

ssize_t co_read_fixed(int32_t fd, void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, len, offset, buf_index](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_read_fixed(sqe, fd, buf, static_cast<unsigned>(len), static_cast<__u64>(offset), buf_index);
    });
}

ssize_t co_write_fixed(int32_t fd, const void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, len, offset, buf_index](struct io_uring_sqe *sqe) noexcept {
        io_uring_prep_write_fixed(sqe, fd, buf, static_cast<unsigned>(len), static_cast<__u64>(offset), buf_index);
    });
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
