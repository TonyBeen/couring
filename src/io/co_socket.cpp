/*************************************************************************
    > File Name: io/co_socket.cpp
    > Author: hsz
    > Brief: Coroutine Socket I/O implementation based on io_uring
    > Created Time: 2026-10-23
 ************************************************************************/

#include "io/co_socket.h"
#include "uring_service.h"
#include "uring_operation.h"
#include "fiber.h"
#include "error_code.h"

#include <liburing.h>
#include <cstring>

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

} // anonymous namespace

int32_t co_accept(int32_t listenfd, struct sockaddr *addr, socklen_t *len, uint64_t timeout_ms) noexcept
{
    return co_accept4(listenfd, addr, len, 0, timeout_ms);
}

int32_t co_accept4(int32_t listenfd, struct sockaddr *addr, socklen_t *len, int32_t flags, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [listenfd, addr, len, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_accept(sqe, listenfd, addr, len, flags);
    });
}

ssize_t co_read(int32_t fd, void *buf, size_t len, uint64_t timeout_ms) noexcept
{
    return co_recv(fd, buf, len, 0, timeout_ms);
}

ssize_t co_write(int32_t fd, const void *buf, size_t len, uint64_t timeout_ms) noexcept
{
    return co_send(fd, buf, len, 0, timeout_ms);
}

ssize_t co_recv(int32_t fd, void *buf, size_t len, int32_t flags, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, len, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_recv(sqe, fd, buf, len, flags);
    });
}

ssize_t co_send(int32_t fd, const void *buf, size_t len, int32_t flags, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, len, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_send(sqe, fd, buf, len, flags);
    });
}

ssize_t co_recvfrom(int32_t fd, void *buf, size_t len, int32_t flags, struct sockaddr *src_addr, socklen_t *addrlen, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    if (!src_addr) {
        return co_recv(fd, buf, len, flags, timeout_ms);
    }

    struct msghdr msg;
    std::memset(&msg, 0, sizeof(msg));
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = len;

    msg.msg_name = src_addr;
    msg.msg_namelen = addrlen ? *addrlen : 0;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    int32_t res = executeOpWithTimeout(service, timeout_ms, [fd, &msg, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_recvmsg(sqe, fd, &msg, flags);
    });

    if (res >= 0 && addrlen) {
        *addrlen = msg.msg_namelen;
    }
    return res;
}

ssize_t co_sendto(int32_t fd, const void *buf, size_t len, int32_t flags, const struct sockaddr *dest_addr, socklen_t addrlen, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    if (!dest_addr) {
        return co_send(fd, buf, len, flags, timeout_ms);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, buf, len, flags, dest_addr, addrlen](struct io_uring_sqe *sqe) {
        io_uring_prep_sendto(sqe, fd, buf, len, flags, dest_addr, addrlen);
    });
}

ssize_t co_recvmsg(int32_t fd, struct msghdr *msg, int32_t flags, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, msg, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_recvmsg(sqe, fd, msg, flags);
    });
}

ssize_t co_sendmsg(int32_t fd, const struct msghdr *msg, int32_t flags, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, msg, flags](struct io_uring_sqe *sqe) {
        io_uring_prep_sendmsg(sqe, fd, msg, flags);
    });
}

int32_t co_recvmmsg(int32_t fd, struct mmsghdr *msgvec, uint32_t vlen, int32_t flags, struct timespec *timeout) noexcept
{
    if (vlen == 0 || !msgvec) {
        return 0;
    }

    uint64_t timeout_ms = 0;
    if (timeout) {
        timeout_ms = static_cast<uint64_t>(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    }

    uint32_t recvd = 0;
    for (uint32_t i = 0; i < vlen; ++i) {
        ssize_t ret = co_recvmsg(fd, &msgvec[i].msg_hdr, flags, timeout_ms);
        if (ret < 0) {
            if (recvd > 0) {
                break;
            }
            return static_cast<int32_t>(ret);
        }
        msgvec[i].msg_len = static_cast<unsigned int>(ret);
        ++recvd;
        timeout_ms = 0;
    }
    return static_cast<int32_t>(recvd);
}

int32_t co_sendmmsg(int32_t fd, struct mmsghdr *msgvec, uint32_t vlen, int32_t flags) noexcept
{
    if (vlen == 0 || !msgvec) {
        return 0;
    }

    uint32_t sent = 0;
    for (uint32_t i = 0; i < vlen; ++i) {
        ssize_t ret = co_sendmsg(fd, &msgvec[i].msg_hdr, flags, 0);
        if (ret < 0) {
            if (sent > 0) {
                break;
            }
            return static_cast<int32_t>(ret);
        }
        msgvec[i].msg_len = static_cast<unsigned int>(ret);
        ++sent;
    }
    return static_cast<int32_t>(sent);
}

int32_t co_connect(int32_t fd, const struct sockaddr *addr, socklen_t len, uint64_t timeout_ms) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, timeout_ms, [fd, addr, len](struct io_uring_sqe *sqe) {
        io_uring_prep_connect(sqe, fd, addr, len);
    });
}

int32_t co_shutdown(int32_t fd, int32_t how) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, 0, [fd, how](struct io_uring_sqe *sqe) {
        io_uring_prep_shutdown(sqe, fd, how);
    });
}

int32_t co_cancel(int32_t fd) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, 0, [fd](struct io_uring_sqe *sqe) {
        io_uring_prep_cancel_fd(sqe, fd, 0);
    });
}

int32_t co_close(int32_t fd) noexcept
{
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return -static_cast<int32_t>(couring::Errc::RingNotInitialized);
    }

    return executeOpWithTimeout(service, 0, [fd](struct io_uring_sqe *sqe) {
        io_uring_prep_close(sqe, fd);
    });
}

} // namespace couring
