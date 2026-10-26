/*************************************************************************
    > File Name: test/test_file_io.cpp
    > Author: hsz
    > Brief: Exhaustive test suite for couring asynchronous File I/O
    > Created Time: 2026-10-26
 ************************************************************************/

#include "catch.hpp"
#include "uring_service.h"
#include "fiber.h"
#include "io/co_file.h"
#include "io/co_socket.h"
#include "sync/co_wait_group.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include <random>

TEST_CASE("File I/O Error and Boundary Conditions", "[file_io_errors]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    service.schedule([&]() noexcept {
        // 1. Invalid path (nullptr) in co_open / co_openat
        REQUIRE(co_open(nullptr, O_RDONLY) == -EINVAL);
        REQUIRE(co_openat(AT_FDCWD, nullptr, O_RDONLY) == -EINVAL);

        // 2. Open non-existent file without O_CREAT
        int res_noent = co_open("/tmp/non_existent_path_couring_12345.tmp", O_RDONLY);
        REQUIRE(res_noent == -ENOENT);

        // 3. Bad file descriptor tests (fd = -1)
        char dummy[16];
        REQUIRE(co_pread(-1, dummy, sizeof(dummy), 0) == -EBADF);
        REQUIRE(co_pwrite(-1, dummy, sizeof(dummy), 0) == -EBADF);
        REQUIRE(co_fallocate(-1, 0, 0, 1024) == -EBADF);
        REQUIRE(co_fsync(-1) == -EBADF);
        REQUIRE(co_fdatasync(-1) == -EBADF);

        // 4. Zero count read/write on valid file
        const char* tmpPath = "/tmp/couring_test_boundary.tmp";
        int fd = co_open(tmpPath, O_CREAT | O_RDWR | O_TRUNC, 0644);
        REQUIRE(fd >= 0);

        REQUIRE(co_pread(fd, dummy, 0, 0) == 0);
        REQUIRE(co_pwrite(fd, dummy, 0, 0) == 0);

        // 5. Read beyond EOF on empty file returns 0 (EOF)
        REQUIRE(co_pread(fd, dummy, sizeof(dummy), 100) == 0);

        // 6. Write to O_RDONLY fd
        co_close(fd);
        int ro_fd = co_open(tmpPath, O_RDONLY);
        REQUIRE(ro_fd >= 0);
        REQUIRE(co_pwrite(ro_fd, "test", 4, 0) == -EBADF);
        co_close(ro_fd);

        // 7. Read from O_WRONLY fd
        int wo_fd = co_open(tmpPath, O_WRONLY);
        REQUIRE(wo_fd >= 0);
        REQUIRE(co_pread(wo_fd, dummy, sizeof(dummy), 0) == -EBADF);
        co_close(wo_fd);

        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

TEST_CASE("File I/O High Concurrency Multi-Fiber Multi-Offset Stress", "[file_io_concurrency]") {
    using namespace eular;
    using namespace couring;

    UringService service(256);
    REQUIRE(service.isValid());

    const char* tmpPath = "/tmp/couring_test_concurrency_stress.tmp";
    unlink(tmpPath);

    constexpr size_t kBlockSize = 4096;
    constexpr size_t kFiberCount = 32;

    service.schedule([&]() noexcept {
        int fd = co_open(tmpPath, O_CREAT | O_RDWR | O_TRUNC, 0644);
        REQUIRE(fd >= 0);

        // Preallocate file to prevent parallel expansion contention
        REQUIRE(co_fallocate(fd, 0, 0, kBlockSize * kFiberCount) == 0);

        CoWaitGroup wg_write;
        wg_write.add(kFiberCount);

        // Concurrent multi-fiber pwrite at different offsets
        for (size_t i = 0; i < kFiberCount; ++i) {
            service.schedule([&, i, fd]() noexcept {
                std::vector<uint8_t> block(kBlockSize);
                for (size_t b = 0; b < kBlockSize; ++b) {
                    block[b] = static_cast<uint8_t>((i * 7 + b * 13) & 0xFF);
                }
                off_t offset = static_cast<off_t>(i * kBlockSize);
                ssize_t w = co_pwrite(fd, block.data(), kBlockSize, offset);
                REQUIRE(w == static_cast<ssize_t>(kBlockSize));
                wg_write.done();
            });
        }

        wg_write.wait();

        // Flush dirty pages
        REQUIRE(co_fdatasync(fd) == 0);

        // Concurrent multi-fiber pread and verify exact byte matching
        CoWaitGroup wg_read;
        wg_read.add(kFiberCount);

        for (size_t i = 0; i < kFiberCount; ++i) {
            service.schedule([&, i, fd]() noexcept {
                std::vector<uint8_t> read_buf(kBlockSize, 0);
                off_t offset = static_cast<off_t>(i * kBlockSize);
                ssize_t r = co_pread(fd, read_buf.data(), kBlockSize, offset);
                REQUIRE(r == static_cast<ssize_t>(kBlockSize));

                // Verify content integrity
                for (size_t b = 0; b < kBlockSize; ++b) {
                    uint8_t expected = static_cast<uint8_t>((i * 7 + b * 13) & 0xFF);
                    if (read_buf[b] != expected) {
                        FAIL("Data mismatch in fiber " << i << " at byte " << b);
                    }
                }
                wg_read.done();
            });
        }

        wg_read.wait();

        co_close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

TEST_CASE("File Vectored I/O Boundary and Complex Scatter/Gather", "[file_io_vectored_edge]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    const char* tmpPath = "/tmp/couring_test_vectored_edge.tmp";
    unlink(tmpPath);

    service.schedule([&]() noexcept {
        int fd = co_open(tmpPath, O_CREAT | O_RDWR | O_TRUNC, 0644);
        REQUIRE(fd >= 0);

        // 6 vectors with varying lengths, including an empty (0-length) vector in the middle
        std::string s1 = "HELLO_";
        std::string s2 = ""; // empty vector
        std::string s3 = "VECTOR_";
        std::string s4 = "WORLD_";
        std::string s5 = std::string(512, 'K');
        std::string s6 = "END!";

        struct iovec out_vec[6];
        out_vec[0].iov_base = const_cast<char*>(s1.data());
        out_vec[0].iov_len  = s1.size();
        out_vec[1].iov_base = const_cast<char*>(s2.data());
        out_vec[1].iov_len  = s2.size();
        out_vec[2].iov_base = const_cast<char*>(s3.data());
        out_vec[2].iov_len  = s3.size();
        out_vec[3].iov_base = const_cast<char*>(s4.data());
        out_vec[3].iov_len  = s4.size();
        out_vec[4].iov_base = const_cast<char*>(s5.data());
        out_vec[4].iov_len  = s5.size();
        out_vec[5].iov_base = const_cast<char*>(s6.data());
        out_vec[5].iov_len  = s6.size();

        size_t total_len = s1.size() + s2.size() + s3.size() + s4.size() + s5.size() + s6.size();

        // Write at offset 128
        ssize_t wv = co_pwritev(fd, out_vec, 6, 128);
        REQUIRE(wv == static_cast<ssize_t>(total_len));

        // Read back with different buffer slicing
        std::vector<char> in1(s1.size());
        std::vector<char> in2_4(s3.size() + s4.size());
        std::vector<char> in5(s5.size());
        std::vector<char> in6(s6.size());

        struct iovec in_vec[4];
        in_vec[0].iov_base = in1.data();
        in_vec[0].iov_len  = in1.size();
        in_vec[1].iov_base = in2_4.data();
        in_vec[1].iov_len  = in2_4.size();
        in_vec[2].iov_base = in5.data();
        in_vec[2].iov_len  = in5.size();
        in_vec[3].iov_base = in6.data();
        in_vec[3].iov_len  = in6.size();

        ssize_t rv = co_preadv(fd, in_vec, 4, 128);
        REQUIRE(rv == static_cast<ssize_t>(total_len));

        REQUIRE(std::string(in1.begin(), in1.end()) == s1);
        REQUIRE(std::string(in2_4.begin(), in2_4.end()) == (s3 + s4));
        REQUIRE(std::string(in5.begin(), in5.end()) == s5);
        REQUIRE(std::string(in6.begin(), in6.end()) == s6);

        // Test co_writev & co_readv (streaming offset = -1)
        lseek(fd, 0, SEEK_SET);
        ssize_t sw = co_writev(fd, out_vec, 6);
        REQUIRE(sw == static_cast<ssize_t>(total_len));

        lseek(fd, 0, SEEK_SET);
        ssize_t sr = co_readv(fd, in_vec, 4);
        REQUIRE(sr == static_cast<ssize_t>(total_len));

        co_close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

TEST_CASE("File Large Transfer and Exact Read/Write Helpers", "[file_io_large_exact]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    const char* tmpPath = "/tmp/couring_test_large_exact.tmp";
    unlink(tmpPath);

    service.schedule([&]() noexcept {
        int fd = co_open(tmpPath, O_CREAT | O_RDWR | O_TRUNC, 0644);
        REQUIRE(fd >= 0);

        // 1. Large 1MB buffer write and read
        constexpr size_t kLargeSize = 1024 * 1024;
        std::vector<uint8_t> large_data(kLargeSize);
        for (size_t i = 0; i < kLargeSize; ++i) {
            large_data[i] = static_cast<uint8_t>((i ^ (i >> 8)) & 0xFF);
        }

        ssize_t w = co_pwrite_all(fd, large_data.data(), kLargeSize, 0);
        REQUIRE(w == static_cast<ssize_t>(kLargeSize));

        std::vector<uint8_t> read_data(kLargeSize, 0);
        ssize_t r = co_pread_exact(fd, read_data.data(), kLargeSize, 0);
        REQUIRE(r == static_cast<ssize_t>(kLargeSize));
        REQUIRE(read_data == large_data);

        // 2. Exact read requesting MORE than file size (EOF behavior)
        // File has exactly 1MB. Request 1.5MB. co_pread_exact should return 1MB (hit EOF).
        std::vector<uint8_t> eof_buf(kLargeSize + 512 * 1024, 0);
        ssize_t r_eof = co_pread_exact(fd, eof_buf.data(), eof_buf.size(), 0);
        REQUIRE(r_eof == static_cast<ssize_t>(kLargeSize));

        // 3. Exact read starting from EOF offset returns 0
        ssize_t r_zero = co_pread_exact(fd, eof_buf.data(), 1024, kLargeSize);
        REQUIRE(r_zero == 0);

        co_close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

TEST_CASE("File Open Flags (O_CREAT | O_EXCL, O_APPEND)", "[file_io_open_flags]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    const char* tmpPath = "/tmp/couring_test_open_flags.tmp";
    unlink(tmpPath);

    service.schedule([&]() noexcept {
        // 1. First open with O_CREAT | O_EXCL succeeds
        int fd1 = co_open(tmpPath, O_CREAT | O_EXCL | O_RDWR, 0644);
        REQUIRE(fd1 >= 0);

        // 2. Second open with O_CREAT | O_EXCL on existing file returns -EEXIST
        int fd2 = co_open(tmpPath, O_CREAT | O_EXCL | O_RDWR, 0644);
        REQUIRE(fd2 == -EEXIST);

        // 3. Test O_APPEND mode
        co_close(fd1);
        int append_fd = co_open(tmpPath, O_WRONLY | O_APPEND);
        REQUIRE(append_fd >= 0);

        ssize_t w1 = co_write_all(append_fd, "PART1", 5);
        REQUIRE(w1 == 5);
        ssize_t w2 = co_write_all(append_fd, "PART2", 5);
        REQUIRE(w2 == 5);

        co_close(append_fd);

        // Verify combined content
        int r_fd = co_open(tmpPath, O_RDONLY);
        REQUIRE(r_fd >= 0);
        char buf[16] = {0};
        ssize_t r = co_read_exact(r_fd, buf, 10);
        REQUIRE(r == 10);
        REQUIRE(std::string(buf, 10) == "PART1PART2");

        co_close(r_fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}
