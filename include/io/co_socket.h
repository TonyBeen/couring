/*************************************************************************
    > File Name: io/co_socket.h
    > Author: hsz
    > Brief: Coroutine Socket I/O API based on io_uring
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_CO_SOCKET_H__
#define __COURING_CO_SOCKET_H__

#include <cstdint>
#include <cstddef>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>

namespace couring {

int32_t co_accept(int32_t listenfd, struct sockaddr *addr, socklen_t *len, uint64_t timeout_ms = 0) noexcept;
int32_t co_accept4(int32_t listenfd, struct sockaddr *addr, socklen_t *len, int32_t flags, uint64_t timeout_ms = 0) noexcept;

ssize_t co_read(int32_t fd, void *buf, size_t len, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write(int32_t fd, const void *buf, size_t len, uint64_t timeout_ms = 0) noexcept;

ssize_t co_recv(int32_t fd, void *buf, size_t len, int32_t flags = 0, uint64_t timeout_ms = 0) noexcept;
ssize_t co_send(int32_t fd, const void *buf, size_t len, int32_t flags = 0, uint64_t timeout_ms = 0) noexcept;

ssize_t co_recvfrom(int32_t fd, void *buf, size_t len, int32_t flags, struct sockaddr *src_addr, socklen_t *addrlen, uint64_t timeout_ms = 0) noexcept;
ssize_t co_sendto(int32_t fd, const void *buf, size_t len, int32_t flags, const struct sockaddr *dest_addr, socklen_t addrlen, uint64_t timeout_ms = 0) noexcept;

ssize_t co_recvmsg(int32_t fd, struct msghdr *msg, int32_t flags = 0, uint64_t timeout_ms = 0) noexcept;
ssize_t co_sendmsg(int32_t fd, const struct msghdr *msg, int32_t flags = 0, uint64_t timeout_ms = 0) noexcept;

int32_t co_recvmmsg(int32_t fd, struct mmsghdr *msgvec, uint32_t vlen, int32_t flags = 0, struct timespec *timeout = nullptr) noexcept;
int32_t co_sendmmsg(int32_t fd, struct mmsghdr *msgvec, uint32_t vlen, int32_t flags = 0) noexcept;

int32_t co_connect(int32_t fd, const struct sockaddr *addr, socklen_t len, uint64_t timeout_ms = 0) noexcept;
int32_t co_shutdown(int32_t fd, int32_t how) noexcept;

int32_t co_cancel(int32_t fd) noexcept;
int32_t co_close(int32_t fd) noexcept;

} // namespace couring

#endif // __COURING_CO_SOCKET_H__
