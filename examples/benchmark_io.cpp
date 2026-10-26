/*************************************************************************
    > File Name: examples/benchmark_io.cpp
    > Author: hsz
    > Brief: Benchmark for couring socket and file read/write performance
    > Created Time: 2026-10-26
 ************************************************************************/

#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <cstring>
#include <atomic>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "uring_service.h"
#include "fiber.h"
#include "logger.h"
#include "io/co_socket.h"
#include "io/co_file.h"
#include "buffer/buffer.h"
#include "sync/co_wait_group.h"

// ----------------------------------------------------------------------------
// Benchmark 1: Socket Streaming Read/Write Throughput
// ----------------------------------------------------------------------------
void benchmark_socket_stream(size_t total_mb = 200, size_t chunk_size = 64 * 1024)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Benchmark 1: Socket Streaming Throughput (co_send / co_recv)\n";
    std::cout << "  Total Data: " << total_mb << " MB, Chunk Size: " << chunk_size / 1024 << " KB\n";
    std::cout << "======================================================\n";

    eular::UringService service(1024);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        return;
    }

    const size_t total_bytes = total_mb * 1024 * 1024;
    couring::CoWaitGroup wg;
    wg.add(2);

    auto start_time = std::chrono::steady_clock::now();

    // Sender coroutine
    service.schedule([&, total_bytes, chunk_size]() noexcept {
        std::vector<char> send_buf(chunk_size, 'X');
        size_t bytes_sent = 0;
        while (bytes_sent < total_bytes) {
            size_t to_send = std::min(chunk_size, total_bytes - bytes_sent);
            ssize_t n = couring::co_send(fds[0], send_buf.data(), to_send, 0);
            if (n <= 0) break;
            bytes_sent += n;
        }
        couring::co_close(fds[0]);
        wg.done();
    });

    // Receiver coroutine
    service.schedule([&, total_bytes, chunk_size]() noexcept {
        std::vector<char> recv_buf(chunk_size);
        size_t bytes_received = 0;
        while (bytes_received < total_bytes) {
            ssize_t n = couring::co_recv(fds[1], recv_buf.data(), chunk_size, 0);
            if (n <= 0) break;
            bytes_received += n;
        }
        couring::co_close(fds[1]);
        wg.done();
    });

    service.schedule([&]() noexcept {
        wg.wait();
        service.stop();
    });

    service.start();

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    double throughput_mb = (total_mb) / duration;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Elapsed Time  : " << duration << " s\n";
    std::cout << "Throughput    : " << throughput_mb << " MB/s (" << (throughput_mb * 8.0) << " Mbps)\n";
}

// ----------------------------------------------------------------------------
// Benchmark 2: Socket Ping-Pong Read/Write Latency & QPS
// ----------------------------------------------------------------------------
void benchmark_socket_pingpong(size_t rounds = 50000, size_t payload_size = 64)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Benchmark 2: Socket Ping-Pong Latency & QPS (Round-Trip)\n";
    std::cout << "  Rounds: " << rounds << ", Payload: " << payload_size << " bytes\n";
    std::cout << "======================================================\n";

    eular::UringService service(1024);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return;
    }

    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        perror("socketpair");
        return;
    }

    couring::CoWaitGroup wg;
    wg.add(2);

    auto start_time = std::chrono::steady_clock::now();

    // Client coroutine
    service.schedule([&, rounds, payload_size]() noexcept {
        std::vector<char> buf(payload_size, 'A');
        for (size_t i = 0; i < rounds; ++i) {
            ssize_t w = couring::co_send(fds[0], buf.data(), payload_size, 0);
            if (w <= 0) break;
            ssize_t r = couring::co_recv(fds[0], buf.data(), payload_size, 0);
            if (r <= 0) break;
        }
        couring::co_close(fds[0]);
        wg.done();
    });

    // Server coroutine (echo)
    service.schedule([&, rounds, payload_size]() noexcept {
        std::vector<char> buf(payload_size);
        for (size_t i = 0; i < rounds; ++i) {
            ssize_t r = couring::co_recv(fds[1], buf.data(), payload_size, 0);
            if (r <= 0) break;
            ssize_t w = couring::co_send(fds[1], buf.data(), r, 0);
            if (w <= 0) break;
        }
        couring::co_close(fds[1]);
        wg.done();
    });

    service.schedule([&]() noexcept {
        wg.wait();
        service.stop();
    });

    service.start();

    auto end_time = std::chrono::steady_clock::now();
    double duration = std::chrono::duration<double>(end_time - start_time).count();
    double qps = rounds / duration;
    double avg_latency_us = (duration / rounds) * 1e6;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Completed     : " << rounds << " round-trips\n";
    std::cout << "Elapsed Time  : " << duration << " s\n";
    std::cout << "Round-Trip QPS: " << static_cast<uint64_t>(qps) << " ops/s\n";
    std::cout << "Avg Latency   : " << avg_latency_us << " us / round-trip\n";
}

// ----------------------------------------------------------------------------
// Benchmark 3: File Asynchronous Read/Write (co_write_file / co_read_file)
// ----------------------------------------------------------------------------
void benchmark_file_io(size_t total_mb = 100, size_t block_size = 64 * 1024)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Benchmark 3: File Asynchronous I/O (co_write_file / co_read_file)\n";
    std::cout << "  File Size: " << total_mb << " MB, Block Size: " << block_size / 1024 << " KB\n";
    std::cout << "======================================================\n";

    eular::UringService service(1024);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return;
    }

    char tmpPath[] = "/tmp/couring_bench_file_XXXXXX";
    int fd = mkstemp(tmpPath);
    if (fd < 0) {
        perror("mkstemp");
        return;
    }

    const size_t total_bytes = total_mb * 1024 * 1024;
    const size_t ops = total_bytes / block_size;

    service.schedule([&, fd, total_bytes, block_size, ops]() noexcept {
        std::vector<char> write_buf(block_size, 'F');
        std::vector<char> read_buf(block_size);

        // --- Test Write ---
        auto t_write_start = std::chrono::steady_clock::now();
        size_t written = 0;
        for (size_t i = 0; i < ops; ++i) {
            ssize_t n = couring::co_write_file(fd, write_buf.data(), block_size, written);
            if (n <= 0) break;
            written += n;
        }
        auto t_write_end = std::chrono::steady_clock::now();

        // --- Test Read ---
        auto t_read_start = std::chrono::steady_clock::now();
        size_t read_total = 0;
        for (size_t i = 0; i < ops; ++i) {
            ssize_t n = couring::co_read_file(fd, read_buf.data(), block_size, read_total);
            if (n <= 0) break;
            read_total += n;
        }
        auto t_read_end = std::chrono::steady_clock::now();

        double w_time = std::chrono::duration<double>(t_write_end - t_write_start).count();
        double r_time = std::chrono::duration<double>(t_read_end - t_read_start).count();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[File Write]  : " << (written / (1024.0 * 1024.0)) << " MB in " << w_time << " s"
                  << " -> " << (written / (1024.0 * 1024.0) / w_time) << " MB/s ("
                  << static_cast<uint64_t>(ops / w_time) << " IOPS)\n";

        std::cout << "[File Read]   : " << (read_total / (1024.0 * 1024.0)) << " MB in " << r_time << " s"
                  << " -> " << (read_total / (1024.0 * 1024.0) / r_time) << " MB/s ("
                  << static_cast<uint64_t>(ops / r_time) << " IOPS)\n";

        close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

// ----------------------------------------------------------------------------
// Benchmark 4: Fixed-Buffer File I/O (co_write_fixed / co_read_fixed)
// ----------------------------------------------------------------------------
void benchmark_file_fixed_io(size_t total_mb = 100, size_t block_size = 64 * 1024)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Benchmark 4: Fixed-Buffer File I/O (co_write_fixed / co_read_fixed)\n";
    std::cout << "  File Size: " << total_mb << " MB, Block Size: " << block_size / 1024 << " KB\n";
    std::cout << "======================================================\n";

    eular::UringService service(1024);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return;
    }

    char tmpPath[] = "/tmp/couring_bench_fixed_XXXXXX";
    int fd = mkstemp(tmpPath);
    if (fd < 0) {
        perror("mkstemp");
        return;
    }

    const size_t total_bytes = total_mb * 1024 * 1024;
    const size_t ops = total_bytes / block_size;

    service.schedule([&, fd, total_bytes, block_size, ops]() noexcept {
        // Prepare registered buffers
        std::vector<couring::ByteBuffer> buffers;
        buffers.emplace_back(block_size);
        buffers[0].resize(block_size);
        std::memset(buffers[0].data(), 'K', block_size);

        couring::BufferRegistration reg(buffers, &service);
        couring::BufferId* bid = reg.acquire();
        if (!bid) {
            std::cerr << "Failed to acquire registered buffer\n";
            close(fd);
            unlink(tmpPath);
            service.stop();
            return;
        }

        // --- Test Write Fixed ---
        auto t_write_start = std::chrono::steady_clock::now();
        size_t written = 0;
        for (size_t i = 0; i < ops; ++i) {
            ssize_t n = couring::co_write_fixed(fd, bid, block_size, written);
            if (n <= 0) break;
            written += n;
        }
        auto t_write_end = std::chrono::steady_clock::now();

        // --- Test Read Fixed ---
        auto t_read_start = std::chrono::steady_clock::now();
        size_t read_total = 0;
        for (size_t i = 0; i < ops; ++i) {
            ssize_t n = couring::co_read_fixed(fd, bid, block_size, read_total);
            if (n <= 0) break;
            read_total += n;
        }
        auto t_read_end = std::chrono::steady_clock::now();

        reg.release(bid);

        double w_time = std::chrono::duration<double>(t_write_end - t_write_start).count();
        double r_time = std::chrono::duration<double>(t_read_end - t_read_start).count();

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "[Fixed Write] : " << (written / (1024.0 * 1024.0)) << " MB in " << w_time << " s"
                  << " -> " << (written / (1024.0 * 1024.0) / w_time) << " MB/s ("
                  << static_cast<uint64_t>(ops / w_time) << " IOPS)\n";

        std::cout << "[Fixed Read]  : " << (read_total / (1024.0 * 1024.0)) << " MB in " << r_time << " s"
                  << " -> " << (read_total / (1024.0 * 1024.0) / r_time) << " MB/s ("
                  << static_cast<uint64_t>(ops / r_time) << " IOPS)\n";

        close(fd);
        unlink(tmpPath);
        service.stop();
    });

    service.start();
}

int main(int argc, char* argv[])
{
    // Silence internal framework logs for pure benchmark output
    couring::set_log_level(couring::LogLevel::SILENT);

    std::cout << "\n======================================================\n";
    std::cout << "           couring I/O Performance Benchmark          \n";
    std::cout << "======================================================\n";

    // 1. Socket Streaming Throughput (64KB chunks)
    benchmark_socket_stream(500, 64 * 1024);

    // 2. Socket Ping-Pong Latency & QPS (100,000 rounds)
    benchmark_socket_pingpong(100000, 64);

    // 3. File Standard Async I/O (4KB block size - IOPS focus)
    benchmark_file_io(50, 4 * 1024);

    // 4. File Standard Async I/O (64KB block size - Throughput focus)
    benchmark_file_io(200, 64 * 1024);

    // 5. File Fixed-Buffer I/O (Registered Buffers, 64KB block size)
    benchmark_file_fixed_io(200, 64 * 1024);

    std::cout << "\n======================================================\n";
    std::cout << "               All Benchmarks Completed               \n";
    std::cout << "======================================================\n\n";
    return 0;
}
