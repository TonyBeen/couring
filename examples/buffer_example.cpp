/*************************************************************************
    > File Name: examples/buffer_example.cpp
    > Author: hsz
    > Brief: Example demonstrating BufferRegistration and BufferProvide usage
    > Created Time: 2026-10-23
 ************************************************************************/

#include <iostream>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

#include "uring_service.h"
#include "fiber.h"
#include "logger.h"
#include "buffer/buffer.h"
#include "io/co_file.h"

// ----------------------------------------------------------------------------
// Example 1: BufferRegistration (Fixed/Registered Buffers)
// ----------------------------------------------------------------------------
// In high-frequency disk I/O, registering memory buffers into io_uring allows
// the Linux kernel to lock (pin) the physical pages upfront.
// Subsequent co_read_fixed / co_write_fixed calls completely bypass page table
// mapping and unpinning overhead, providing maximum I/O throughput.
void demo_buffer_registration(eular::UringService& service) noexcept
{
    std::cout << "\n=== [Demo 1] BufferRegistration (Fixed Buffers) ===\n";

    constexpr size_t kBlockSize = 4096;
    constexpr size_t kBufferCount = 4;

    // 1. Prepare user memory buffers
    std::vector<eular::ByteBuffer> buffers;
    for (size_t i = 0; i < kBufferCount; ++i) {
        buffers.emplace_back(kBlockSize);
    }

    // 2. Register all buffers with io_uring upfront
    //    Kernel pins physical pages and sets up kernel mapping table.
    couring::BufferRegistration reg(buffers, &service);
    std::cout << "Registered " << reg.size() << " buffers (" << kBlockSize << " bytes each) to io_uring.\n";

    // 3. Acquire a buffer from the pool (O(1) bitmap allocation)
    couring::BufferId* bid = reg.acquire();
    if (!bid) {
        std::cerr << "Failed to acquire buffer from registration pool\n";
        return;
    }

    std::cout << "Acquired buffer index: " << bid->bid()
              << ", address: " << bid->buffer()
              << ", capacity: " << bid->size() << " bytes\n";

    // Fill buffer with test payload
    const char msg[] = "High-performance fixed-buffer file write via io_uring!";
    std::memcpy(bid->buffer(), msg, sizeof(msg));

    // 4. Perform Fixed-Buffer File I/O
    char tmpPath[] = "/tmp/couring_buffer_reg_demo_XXXXXX";
    int fd = mkstemp(tmpPath);
    if (fd < 0) {
        std::cerr << "Failed to create temp file\n";
        reg.release(bid);
        return;
    }

    std::cout << "Writing payload using co_write_fixed to: " << tmpPath << "\n";
    ssize_t written = couring::co_write_fixed(fd, bid, sizeof(msg), 0);
    std::cout << "Bytes written: " << written << "\n";

    // Clear local memory to verify read
    std::memset(bid->buffer(), 0, sizeof(msg));

    std::cout << "Reading payload back using co_read_fixed...\n";
    ssize_t read_bytes = couring::co_read_fixed(fd, bid, sizeof(msg), 0);
    std::cout << "Bytes read back: " << read_bytes << "\n";
    std::cout << "Read content: \"" << static_cast<char*>(bid->buffer()) << "\"\n";

    // 5. Release buffer back to the pool
    reg.release(bid);
    std::cout << "Buffer released back to registration pool.\n";

    close(fd);
    unlink(tmpPath);
}

// ----------------------------------------------------------------------------
// Example 2: BufferProvide (Kernel-Provided Buffer Pool)
// ----------------------------------------------------------------------------
// Useful for massive concurrent socket connections (e.g. 50k+ connections).
// Instead of allocating a dedicated 64KB receive buffer for each idle connection,
// buffers are provided to an io_uring buffer group (gid). When a packet arrives,
// the kernel dynamically selects an available buffer from this group.
void demo_buffer_provide(eular::UringService& service) noexcept
{
    std::cout << "\n=== [Demo 2] BufferProvide (Kernel-Provided Buffers) ===\n";

    constexpr size_t kBufSize = 2048;
    constexpr int32_t kGroupId = 10;

    eular::ByteBuffer buf(kBufSize);

    // 1. Provide the buffer to kernel group gid = 10
    //    This submits IORING_OP_PROVIDE_BUFFERS to kernel and awaits confirmation.
    couring::BufferProvide provider(buf, &service, kGroupId);
    std::cout << "Provided buffer (size=" << kBufSize << ") to kernel group ID: " << kGroupId << "\n";

    // 2. Acquire buffer handle
    couring::BufferId* id = provider.acquire();
    if (id) {
        std::cout << "Acquired provided buffer, group id=" << id->gid()
                  << ", buffer ptr=" << id->buffer()
                  << ", size=" << id->size() << "\n";

        // When in use, provider prevents duplicate acquisition
        bool reacquire = (provider.acquire() != nullptr);
        std::cout << "Can reacquire while in use: " << (reacquire ? "yes" : "no (properly protected)") << "\n";

        // 3. Return buffer back to kernel pool
        provider.release(id);
        std::cout << "Released buffer back to kernel group.\n";
    }
}

int main()
{
    couring::set_log_level(couring::LogLevel::WARN);

    eular::UringService service(256);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return 1;
    }

    service.schedule([&service]() noexcept {
        demo_buffer_registration(service);
        demo_buffer_provide(service);
        service.stop();
    });

    std::cout << "Starting UringService event loop...\n";
    service.start();

    std::cout << "\nAll buffer demos completed successfully!\n";
    return 0;
}
