/*************************************************************************
    > File Name: examples/benchmark_concurrency.cpp
    > Author: hsz
    > Brief: Benchmark for couring single-thread fiber & network concurrency
    > Created Time: 2026-10-23
 ************************************************************************/

#include <iostream>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstring>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "uring_service.h"
#include "fiber.h"
#include "logger.h"
#include "io/co_socket.h"
#include "sync/co_wait_group.h"

static size_t get_memory_rss_kb()
{
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) return 0;
    char line[256];
    size_t rss = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%zu", &rss);
            break;
        }
    }
    fclose(fp);
    return rss;
}

// ---------------------------------------------------------
// Test 1: Pure Fiber Scheduling Concurrency (100,000 Fibers)
// ---------------------------------------------------------
void test_pure_fiber_concurrency(size_t fiber_count, int yields_per_fiber)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Test 1: Single-thread Pure Fiber Concurrency\n";
    std::cout << "  Fibers: " << fiber_count << ", Yields per fiber: " << yields_per_fiber << "\n";
    std::cout << "======================================================\n";

    couring::set_log_level(couring::LogLevel::SILENT);

    size_t mem_start = get_memory_rss_kb();
    eular::Scheduler sc("FiberBench");

    std::atomic<uint64_t> total_switches{0};
    couring::CoWaitGroup wg;
    wg.add(fiber_count);

    auto start_time = std::chrono::steady_clock::now();

    for (size_t i = 0; i < fiber_count; ++i) {
        sc.schedule([&, yields_per_fiber]() noexcept {
            for (int y = 0; y < yields_per_fiber; ++y) {
                total_switches.fetch_add(1, std::memory_order_relaxed);
                eular::Fiber::Yield2Ready();
            }
            wg.done();
        });
    }

    sc.schedule([&]() noexcept {
        wg.wait();
        sc.stop();
    });

    sc.start();

    auto end_time = std::chrono::steady_clock::now();
    size_t mem_end = get_memory_rss_kb();

    std::chrono::duration<double> diff = end_time - start_time;
    double total_sec = diff.count();
    uint64_t switches = total_switches.load();
    double qps = switches / total_sec;

    std::cout << "Finished in        : " << total_sec << " seconds\n";
    std::cout << "Total context switches: " << switches << "\n";
    std::cout << "Switching Throughput: " << static_cast<uint64_t>(qps) << " switches/sec\n";
    std::cout << "Average switch cost : " << (total_sec * 1e9 / switches) << " ns/switch\n";
    std::cout << "Memory RSS increase : " << (mem_end > mem_start ? mem_end - mem_start : 0) / 1024.0 << " MB\n";
    std::cout << "Average fiber memory: ~" << ((mem_end > mem_start ? mem_end - mem_start : 0) * 1024.0 / fiber_count) << " bytes/fiber\n";
}

// ---------------------------------------------------------
// Test 2: Network TCP Echo Concurrency (Single Thread 3,000 Clients)
// ---------------------------------------------------------
void test_network_concurrency(size_t client_count, uint16_t port)
{
    std::cout << "\n======================================================\n";
    std::cout << "  Test 2: Single-thread Network TCP Concurrency\n";
    std::cout << "  Concurrent Clients: " << client_count << ", Port: " << port << "\n";
    std::cout << "======================================================\n";

    couring::set_log_level(couring::LogLevel::SILENT);

    eular::UringService service(4096);
    if (!service.isValid()) {
        std::cerr << "Failed to init UringService\n";
        return;
    }

    int32_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0 ||
        listen(listen_fd, 4096) != 0) {
        std::cerr << "Failed to bind/listen\n";
        close(listen_fd);
        return;
    }

    std::atomic<size_t> server_handled{0};
    std::atomic<size_t> client_success{0};
    couring::CoWaitGroup wg;
    wg.add(client_count);

    // Accept loop coroutine
    service.schedule([&, listen_fd]() noexcept {
        while (server_handled.load() < client_count) {
            sockaddr_in caddr{};
            socklen_t clen = sizeof(caddr);
            int32_t cfd = couring::co_accept(listen_fd, reinterpret_cast<sockaddr*>(&caddr), &clen);
            if (cfd < 0) break;

            service.schedule([&, cfd]() noexcept {
                char buf[128];
                ssize_t n = couring::co_recv(cfd, buf, sizeof(buf), 0, 5000);
                if (n > 0) {
                    couring::co_send(cfd, buf, n, 0, 5000);
                }
                couring::co_close(cfd);
                server_handled.fetch_add(1, std::memory_order_relaxed);
            });
        }
        couring::co_close(listen_fd);
    });

    auto start_time = std::chrono::steady_clock::now();

    // Spawn concurrent clients
    for (size_t i = 0; i < client_count; ++i) {
        service.schedule([&, port, i]() noexcept {
            int32_t fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd >= 0) {
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

                if (couring::co_connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr), 5000) == 0) {
                    const char msg[] = "ping";
                    if (couring::co_send(fd, msg, 4, 0, 5000) == 4) {
                        char resp[128];
                        if (couring::co_recv(fd, resp, sizeof(resp), 0, 5000) == 4) {
                            if (memcmp(msg, resp, 4) == 0) {
                                client_success.fetch_add(1, std::memory_order_relaxed);
                            }
                        }
                    }
                }
                couring::co_close(fd);
            }
            wg.done();
        });
    }

    // Waiter fiber
    service.schedule([&]() noexcept {
        wg.wait();
        service.stop();
    });

    service.start();

    auto end_time = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
    double sec = diff.count();

    std::cout << "Connections completed : " << client_success.load() << " / " << client_count << "\n";
    std::cout << "Total time            : " << sec << " seconds\n";
    std::cout << "Network Echo QPS      : " << static_cast<uint64_t>(client_success.load() / sec) << " req/sec\n";
    std::cout << "Avg Latency per Conn  : " << (sec / client_count * 1000.0) << " ms\n";
}

int main(int argc, char* argv[])
{
    // Adjust file limit
    struct rlimit rlim;
    rlim.rlim_cur = 65535;
    rlim.rlim_max = 65535;
    setrlimit(RLIMIT_NOFILE, &rlim);

    size_t fiber_count = (argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 100000);
    size_t conn_count = (argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 5000);

    // 1. Pure coroutine concurrency
    test_pure_fiber_concurrency(fiber_count, 5);

    // 2. Real network concurrency
    test_network_concurrency(conn_count, 10888);

    return 0;
}
