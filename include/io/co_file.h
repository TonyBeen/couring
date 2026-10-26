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
#include <sys/uio.h>
#include <fcntl.h>

namespace couring {

class BufferId;

// ----------------------------------------------------------------------------
// File Open
// ----------------------------------------------------------------------------
/**
 * @brief Asynchronously open a file relative to a directory file descriptor
 */
int co_openat(int32_t dfd, const char *path, int flags, mode_t mode = 0, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously open a file relative to current working directory (AT_FDCWD)
 */
int co_open(const char *path, int flags, mode_t mode = 0, uint64_t timeout_ms = 0) noexcept;

// ----------------------------------------------------------------------------
// Positional I/O (pread / pwrite)
// ----------------------------------------------------------------------------
/**
 * @brief Asynchronously read from file at specified offset without altering file position
 */
ssize_t co_pread(int32_t fd, void *buf, size_t count, off_t offset, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously write to file at specified offset without altering file position
 */
ssize_t co_pwrite(int32_t fd, const void *buf, size_t count, off_t offset, uint64_t timeout_ms = 0) noexcept;

// Compatibility aliases
inline ssize_t co_read_file(int32_t fd, void *buf, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept {
    return co_pread(fd, buf, len, static_cast<off_t>(offset), timeout_ms);
}

inline ssize_t co_write_file(int32_t fd, const void *buf, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept {
    return co_pwrite(fd, buf, len, static_cast<off_t>(offset), timeout_ms);
}

// ----------------------------------------------------------------------------
// Vectored I/O (Scatter / Gather: preadv / pwritev / readv / writev)
// ----------------------------------------------------------------------------
/**
 * @brief Asynchronously scatter read from file at specified offset into multiple buffers
 */
ssize_t co_preadv(int32_t fd, const struct iovec *iov, int32_t iovcnt, off_t offset, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously gather write multiple buffers to file at specified offset
 */
ssize_t co_pwritev(int32_t fd, const struct iovec *iov, int32_t iovcnt, off_t offset, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously scatter read from file at current file offset into multiple buffers
 */
ssize_t co_readv(int32_t fd, const struct iovec *iov, int32_t iovcnt, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously gather write multiple buffers to file at current file offset
 */
ssize_t co_writev(int32_t fd, const struct iovec *iov, int32_t iovcnt, uint64_t timeout_ms = 0) noexcept;

// ----------------------------------------------------------------------------
// Full Read/Write Helpers (Short read/write loops)
// ----------------------------------------------------------------------------
/**
 * @brief Read exactly len bytes from file at specified offset (looping on short reads)
 * @return Bytes read (equal to len on success, < len on EOF, or negative error code)
 */
ssize_t co_pread_exact(int32_t fd, void *buf, size_t len, off_t offset, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Write all len bytes to file at specified offset (looping on short writes)
 * @return Total bytes written (equal to len on success, or negative error code)
 */
ssize_t co_pwrite_all(int32_t fd, const void *buf, size_t len, off_t offset, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Read exactly len bytes from file/socket at current position
 */
ssize_t co_read_exact(int32_t fd, void *buf, size_t len, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Write all len bytes to file/socket at current position
 */
ssize_t co_write_all(int32_t fd, const void *buf, size_t len, uint64_t timeout_ms = 0) noexcept;

// ----------------------------------------------------------------------------
// File Preallocation & Sync
// ----------------------------------------------------------------------------
/**
 * @brief Asynchronously preallocate contiguous disk blocks for file
 */
int co_fallocate(int32_t fd, int32_t mode, off_t offset, off_t len, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously flush file data and metadata to disk
 */
int co_fsync(int32_t fd, uint64_t timeout_ms = 0) noexcept;

/**
 * @brief Asynchronously flush file data (excluding non-essential metadata) to disk
 */
int co_fdatasync(int32_t fd, uint64_t timeout_ms = 0) noexcept;

// ----------------------------------------------------------------------------
// Fixed / Registered Buffer File I/O
// ----------------------------------------------------------------------------
ssize_t co_read_fixed(int32_t fd, void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write_fixed(int32_t fd, const void *buf, size_t len, int64_t offset, int32_t buf_index, uint64_t timeout_ms = 0) noexcept;

ssize_t co_read_fixed(int32_t fd, BufferId *bid, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;
ssize_t co_write_fixed(int32_t fd, const BufferId *bid, size_t len, int64_t offset = 0, uint64_t timeout_ms = 0) noexcept;

} // namespace couring

namespace eular {
using couring::co_open;
using couring::co_openat;
using couring::co_pread;
using couring::co_pwrite;
using couring::co_read_file;
using couring::co_write_file;
using couring::co_preadv;
using couring::co_pwritev;
using couring::co_readv;
using couring::co_writev;
using couring::co_pread_exact;
using couring::co_pwrite_all;
using couring::co_read_exact;
using couring::co_write_all;
using couring::co_fallocate;
using couring::co_fsync;
using couring::co_fdatasync;
using couring::co_read_fixed;
using couring::co_write_fixed;
} // namespace eular

#endif // __COURING_CO_FILE_H__
