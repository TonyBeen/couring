/*************************************************************************
    > File Name: hook.cpp
    > Author: hsz
    > Brief: System call hooking implementation for couring
    > Created Time: 2026-10-23
 ************************************************************************/

#include "hook.h"
#include "fiber.h"
#include "scheduler.h"
#include "uring_service.h"
#include "inner/fd_manager.h"
#include "io/co_socket.h"
#include "io/co_file.h"
#include "io/co_timer.h"

#include <dlfcn.h>
#include <cerrno>
#include <cstdarg>
#include <fcntl.h>

namespace eular {
namespace hook {

static thread_local bool g_hook_enabled = false;

bool is_hook_enabled() noexcept
{
    return g_hook_enabled;
}

void set_hook_enabled(bool flag) noexcept
{
    g_hook_enabled = flag;
}

} // namespace hook
} // namespace eular

extern "C" {

#define HOOK_FUN(XX)    \
    XX(sleep)           \
    XX(usleep)          \
    XX(nanosleep)       \
    XX(socket)          \
    XX(connect)         \
    XX(accept)          \
    XX(accept4)         \
    XX(shutdown)        \
    XX(close)           \
    XX(read)            \
    XX(write)           \
    XX(recv)            \
    XX(send)            \
    XX(recvfrom)        \
    XX(sendto)          \
    XX(recvmsg)         \
    XX(sendmsg)         \
    XX(recvmmsg)        \
    XX(sendmmsg)        \
    XX(fcntl)           \
    XX(setsockopt)      \
    XX(getsockopt)

#define DECLARE_FUN_PTR(name) name##_fun name##_f = nullptr;
HOOK_FUN(DECLARE_FUN_PTR)
#undef DECLARE_FUN_PTR

#define ASSURE_HOOK(name) \
    do { \
        if (!name##_f) { \
            name##_f = reinterpret_cast<name##_fun>(dlsym(RTLD_NEXT, #name)); \
        } \
    } while (0)

static void init_hooks() noexcept
{
#define INIT_FUN_PTR(name) name##_f = reinterpret_cast<name##_fun>(dlsym(RTLD_NEXT, #name));
    HOOK_FUN(INIT_FUN_PTR)
#undef INIT_FUN_PTR
}

struct HookInitializer {
    HookInitializer() noexcept
    {
        init_hooks();
    }
};

static HookInitializer g_hook_initializer;

static inline bool can_hook() noexcept
{
    if (!eular::hook::is_hook_enabled()) {
        return false;
    }
    // Must be executing inside a worker coroutine (id > 1)
    if (eular::Fiber::GetFiberID() <= 1) {
        return false;
    }
    eular::UringService* service = eular::UringService::GetThis();
    if (!service || !service->isValid()) {
        return false;
    }
    return true;
}

unsigned int sleep(unsigned int seconds)
{
    ASSURE_HOOK(sleep);
    if (can_hook()) {
        (void)couring::co_msleep(static_cast<uint64_t>(seconds) * 1000);
        return 0;
    }
    return sleep_f(seconds);
}

int usleep(useconds_t usec)
{
    ASSURE_HOOK(usleep);
    if (can_hook()) {
        uint64_t ms = (usec + 999) / 1000;
        (void)couring::co_msleep(ms);
        return 0;
    }
    return usleep_f(usec);
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    ASSURE_HOOK(nanosleep);
    if (can_hook() && req) {
        uint64_t ms = static_cast<uint64_t>(req->tv_sec) * 1000 + (req->tv_nsec + 999999) / 1000000;
        (void)couring::co_msleep(ms);
        if (rem) {
            rem->tv_sec = 0;
            rem->tv_nsec = 0;
        }
        return 0;
    }
    return nanosleep_f(req, rem);
}

int socket(int domain, int type, int protocol)
{
    ASSURE_HOOK(socket);
    int fd = socket_f(domain, type, protocol);
    if (fd >= 0) {
        auto ctx = eular::FdManager::Get()->get(fd, true);
        if (ctx && (type & SOCK_NONBLOCK)) {
            ctx->setUserNonblock(true);
        }
    }
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    ASSURE_HOOK(connect);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_SNDTIMEO) : 0;
        int32_t res = couring::co_connect(sockfd, addr, addrlen, timeout_ms);
        if (res < 0) {
            errno = -res;
            return -1;
        }
        return 0;
    }
    return connect_f(sockfd, addr, addrlen);
}

int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    ASSURE_HOOK(accept);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        int32_t res = couring::co_accept(sockfd, addr, addrlen, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : -res;
            return -1;
        }
        eular::FdManager::Get()->get(res, true);
        return res;
    }
    int res = accept_f(sockfd, addr, addrlen);
    if (res >= 0) {
        eular::FdManager::Get()->get(res, true);
    }
    return res;
}

int accept4(int sockfd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
    ASSURE_HOOK(accept4);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        int32_t res = couring::co_accept4(sockfd, addr, addrlen, flags, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : -res;
            return -1;
        }
        auto cctx = eular::FdManager::Get()->get(res, true);
        if (cctx && (flags & SOCK_NONBLOCK)) {
            cctx->setUserNonblock(true);
        }
        return res;
    }
    int res = accept4_f(sockfd, addr, addrlen, flags);
    if (res >= 0) {
        auto cctx = eular::FdManager::Get()->get(res, true);
        if (cctx && (flags & SOCK_NONBLOCK)) {
            cctx->setUserNonblock(true);
        }
    }
    return res;
}

int shutdown(int sockfd, int how)
{
    ASSURE_HOOK(shutdown);
    if (can_hook() && sockfd >= 0) {
        int32_t res = couring::co_shutdown(sockfd, how);
        if (res < 0) {
            errno = -res;
            return -1;
        }
        return 0;
    }
    return shutdown_f(sockfd, how);
}

int close(int fd)
{
    ASSURE_HOOK(close);
    eular::FdManager::Get()->remove(fd);
    // Never intercept standard descriptor closes (stdin, stdout, stderr)
    if (can_hook() && fd > 2) {
        int32_t res = couring::co_close(fd);
        if (res < 0) {
            errno = -res;
            return -1;
        }
        return 0;
    }
    return close_f(fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
    ASSURE_HOOK(read);
    if (fd <= 2) {
        return read_f(fd, buf, count);
    }
    if (can_hook() && fd >= 0) {
        auto ctx = eular::FdManager::Get()->get(fd);
        if (ctx && ctx->isUserNonblock()) {
            return read_f(fd, buf, count);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        ssize_t res = couring::co_read(fd, buf, count, timeout_ms);
        if (res == -ENOTSOCK) {
            res = couring::co_read_file(fd, buf, count, -1, timeout_ms);
        }
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return read_f(fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
    ASSURE_HOOK(write);
    if (fd <= 2) {
        return write_f(fd, buf, count);
    }
    if (can_hook() && fd >= 0) {
        auto ctx = eular::FdManager::Get()->get(fd);
        if (ctx && ctx->isUserNonblock()) {
            return write_f(fd, buf, count);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_SNDTIMEO) : 0;
        ssize_t res = couring::co_write(fd, buf, count, timeout_ms);
        if (res == -ENOTSOCK) {
            res = couring::co_write_file(fd, buf, count, -1, timeout_ms);
        }
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return write_f(fd, buf, count);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
    ASSURE_HOOK(recv);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return recv_f(sockfd, buf, len, flags);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        ssize_t res = couring::co_recv(sockfd, buf, len, flags, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return recv_f(sockfd, buf, len, flags);
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
    ASSURE_HOOK(send);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return send_f(sockfd, buf, len, flags);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_SNDTIMEO) : 0;
        ssize_t res = couring::co_send(sockfd, buf, len, flags, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return send_f(sockfd, buf, len, flags);
}

ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen)
{
    ASSURE_HOOK(recvfrom);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return recvfrom_f(sockfd, buf, len, flags, src_addr, addrlen);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        ssize_t res = couring::co_recvfrom(sockfd, buf, len, flags, src_addr, addrlen, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return recvfrom_f(sockfd, buf, len, flags, src_addr, addrlen);
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
    ASSURE_HOOK(sendto);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return sendto_f(sockfd, buf, len, flags, dest_addr, addrlen);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_SNDTIMEO) : 0;
        ssize_t res = couring::co_sendto(sockfd, buf, len, flags, dest_addr, addrlen, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return sendto_f(sockfd, buf, len, flags, dest_addr, addrlen);
}

ssize_t recvmsg(int sockfd, struct msghdr *msg, int flags)
{
    ASSURE_HOOK(recvmsg);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return recvmsg_f(sockfd, msg, flags);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_RCVTIMEO) : 0;
        ssize_t res = couring::co_recvmsg(sockfd, msg, flags, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return recvmsg_f(sockfd, msg, flags);
}

ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
    ASSURE_HOOK(sendmsg);
    if (can_hook() && sockfd >= 0) {
        auto ctx = eular::FdManager::Get()->get(sockfd);
        if (ctx && ctx->isUserNonblock()) {
            return sendmsg_f(sockfd, msg, flags);
        }
        uint64_t timeout_ms = ctx ? ctx->getTimeout(SO_SNDTIMEO) : 0;
        ssize_t res = couring::co_sendmsg(sockfd, msg, flags, timeout_ms);
        if (res < 0) {
            errno = (res == -ETIMEDOUT) ? EAGAIN : static_cast<int>(-res);
            return -1;
        }
        return res;
    }
    return sendmsg_f(sockfd, msg, flags);
}

int recvmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags, struct timespec *timeout)
{
    ASSURE_HOOK(recvmmsg);
    if (can_hook() && sockfd >= 0) {
        int32_t res = couring::co_recvmmsg(sockfd, msgvec, vlen, flags, timeout);
        if (res < 0) {
            errno = -res;
            return -1;
        }
        return res;
    }
    return recvmmsg_f(sockfd, msgvec, vlen, flags, timeout);
}

int sendmmsg(int sockfd, struct mmsghdr *msgvec, unsigned int vlen, int flags)
{
    ASSURE_HOOK(sendmmsg);
    if (can_hook() && sockfd >= 0) {
        int32_t res = couring::co_sendmmsg(sockfd, msgvec, vlen, flags);
        if (res < 0) {
            errno = -res;
            return -1;
        }
        return res;
    }
    return sendmmsg_f(sockfd, msgvec, vlen, flags);
}

int fcntl(int fd, int cmd, ...)
{
    ASSURE_HOOK(fcntl);
    va_list va;
    va_start(va, cmd);
    void *arg = va_arg(va, void*);
    va_end(va);

    if (cmd == F_SETFL) {
        auto ctx = eular::FdManager::Get()->get(fd);
        if (ctx) {
            int fl = static_cast<int>(reinterpret_cast<intptr_t>(arg));
            ctx->setUserNonblock((fl & O_NONBLOCK) != 0);
        }
    }
    return fcntl_f(fd, cmd, arg);
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    ASSURE_HOOK(setsockopt);
    if (level == SOL_SOCKET && (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)) {
        if (optval && optlen >= sizeof(struct timeval)) {
            const struct timeval *tv = static_cast<const struct timeval*>(optval);
            uint64_t ms = static_cast<uint64_t>(tv->tv_sec * 1000 + tv->tv_usec / 1000);
            auto ctx = eular::FdManager::Get()->get(sockfd, true);
            if (ctx) {
                ctx->setTimeout(optname, ms);
            }
        }
    }
    return setsockopt_f(sockfd, level, optname, optval, optlen);
}

int getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    ASSURE_HOOK(getsockopt);
    return getsockopt_f(sockfd, level, optname, optval, optlen);
}

} // extern "C"
