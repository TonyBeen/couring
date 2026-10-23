/*************************************************************************
    > File Name: io/co_file.h
    > Author: hsz
    > Brief: Coroutine asynchronous File I/O API based on io_uring
    > Created Time: 2026-10-23
 ************************************************************************/

#ifndef __COURING_CO_FILE_H__
#define __COURING_CO_FILE_H__

#include <cstdint>
#include <cstddef>
#include <sys/types.h>

namespace eular {
class BufferId;
}

namespace couring {

using BufferId = eular::BufferId;

ssize_t co_read_file(int32_t fd, void *buf, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write_file(int32_t fd, const void *buf, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;

// Fixed / Registered Buffer File I/O
ssize_t co_read_fixed(int32_t fd, void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write_fixed(int32_t fd, const void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms = 0) noexcept;

ssize_t co_read_fixed(int32_t fd, BufferId *bid, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write_fixed(int32_t fd, const BufferId *bid, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;

} // namespace couring

#endif // __COURING_CO_FILE_H__
