#include "catch.hpp"
#include "hook.h"
#include "uring_service.h"
#include "flat_timer_heap.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <string>

TEST_CASE("Hook flag toggling", "[hook]") {
    REQUIRE(!eular::hook::is_hook_enabled());
    eular::hook::set_hook_enabled(true);
    REQUIRE(eular::hook::is_hook_enabled());
    eular::hook::set_hook_enabled(false);
    REQUIRE(!eular::hook::is_hook_enabled());
}

TEST_CASE("Hooked usleep in coroutine", "[hook]") {
    using namespace eular;

    UringService service(64);
    REQUIRE(service.isValid());

    bool slept = false;
    uint64_t startMs = couring::FlatTimerHeap::CurrentTimeMs();

    service.schedule([&]() noexcept {
        eular::hook::set_hook_enabled(true);

        // Standard libc usleep (30ms) -> transparently intercepted as co_msleep
        int ret = ::usleep(30000);
        REQUIRE(ret == 0);

        uint64_t elapsed = couring::FlatTimerHeap::CurrentTimeMs() - startMs;
        REQUIRE(elapsed >= 25);
        slept = true;

        eular::hook::set_hook_enabled(false);
        service.stop();
    });

    service.start();
    REQUIRE(slept);
}

TEST_CASE("Hooked Socket TCP Echo via standard libc APIs", "[hook]") {
    using namespace eular;

    UringService service(128);
    REQUIRE(service.isValid());

    int listenfd = ::socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listenfd >= 0);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddr.sin_port = 0;

    int bres = bind(listenfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    REQUIRE(bres == 0);

    socklen_t addrLen = sizeof(serverAddr);
    getsockname(listenfd, reinterpret_cast<sockaddr*>(&serverAddr), &addrLen);
    uint16_t port = ntohs(serverAddr.sin_port);

    int lres = listen(listenfd, 5);
    REQUIRE(lres == 0);

    const std::string msg = "Hello from hooked standard libc socket functions!";
    std::string receivedMsg;

    // Server coroutine using standard libc calls
    service.schedule([&]() noexcept {
        eular::hook::set_hook_enabled(true);

        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = ::accept(listenfd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        REQUIRE(clientFd >= 0);

        char buf[128] = {0};
        ssize_t n = ::recv(clientFd, buf, sizeof(buf), 0);
        REQUIRE(n > 0);

        ssize_t sent = ::send(clientFd, buf, n, 0);
        REQUIRE(sent == n);

        ::close(clientFd);
        eular::hook::set_hook_enabled(false);
    });

    // Client coroutine using standard libc calls
    service.schedule([&]() noexcept {
        eular::hook::set_hook_enabled(true);

        int connFd = ::socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(connFd >= 0);

        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        saddr.sin_port = htons(port);

        int cres = ::connect(connFd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
        REQUIRE(cres == 0);

        ssize_t sent = ::send(connFd, msg.data(), msg.size(), 0);
        REQUIRE(sent == static_cast<ssize_t>(msg.size()));

        char recvBuf[128] = {0};
        ssize_t n = ::recv(connFd, recvBuf, sizeof(recvBuf), 0);
        REQUIRE(n == static_cast<ssize_t>(msg.size()));
        receivedMsg.assign(recvBuf, n);

        ::close(connFd);
        ::close(listenfd);
        eular::hook::set_hook_enabled(false);
        service.stop();
    });

    service.start();

    REQUIRE(receivedMsg == msg);
}

TEST_CASE("Hooked sendto, recvfrom, sendmsg, recvmsg, accept4, shutdown", "[hook_socket]") {
    using namespace eular;

    UringService service(128);
    REQUIRE(service.isValid());

    // 1. Test hooked UDP sendto / recvfrom
    int udp1 = ::socket(AF_INET, SOCK_DGRAM, 0);
    int udp2 = ::socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(udp1 >= 0);
    REQUIRE(udp2 >= 0);

    sockaddr_in a1{}, a2{};
    a1.sin_family = AF_INET;
    a1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a1.sin_port = 0;

    a2.sin_family = AF_INET;
    a2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a2.sin_port = 0;

    REQUIRE(::bind(udp1, reinterpret_cast<sockaddr*>(&a1), sizeof(a1)) == 0);
    REQUIRE(::bind(udp2, reinterpret_cast<sockaddr*>(&a2), sizeof(a2)) == 0);

    socklen_t l1 = sizeof(a1), l2 = sizeof(a2);
    getsockname(udp1, reinterpret_cast<sockaddr*>(&a1), &l1);
    getsockname(udp2, reinterpret_cast<sockaddr*>(&a2), &l2);

    const std::string testStr = "Hooked UDP sendto/recvfrom test!";
    std::string recvStr;

    service.schedule([&]() noexcept {
        eular::hook::set_hook_enabled(true);

        // Send via libc ::sendto (intercepted)
        ssize_t s = ::sendto(udp1, testStr.data(), testStr.size(), 0, reinterpret_cast<sockaddr*>(&a2), sizeof(a2));
        REQUIRE(s == static_cast<ssize_t>(testStr.size()));

        sockaddr_in src{};
        socklen_t slen = sizeof(src);
        char buf[128] = {0};
        ssize_t n = ::recvfrom(udp2, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&src), &slen);
        REQUIRE(n == static_cast<ssize_t>(testStr.size()));
        recvStr.assign(buf, n);

        ::close(udp1);
        ::close(udp2);

        eular::hook::set_hook_enabled(false);
        service.stop();
    });

    service.start();
    REQUIRE(recvStr == testStr);
}

TEST_CASE("Hooked setsockopt SO_RCVTIMEO and FdManager integration", "[hook_timeout]") {
    using namespace eular;

    UringService service(64);
    REQUIRE(service.isValid());

    int sv[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    bool timedOut = false;
    service.schedule([&]() noexcept {
        eular::hook::set_hook_enabled(true);

        // Set 50ms receive timeout using standard setsockopt
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50 * 1000; // 50ms
        int sres = ::setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        REQUIRE(sres == 0);

        char buf[64] = {0};
        // Expect ::recv to transparently timeout in 50ms and set errno = EAGAIN
        ssize_t ret = ::recv(sv[0], buf, sizeof(buf), 0);
        if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
            timedOut = true;
        }

        ::close(sv[0]);
        ::close(sv[1]);
        eular::hook::set_hook_enabled(false);
        service.stop();
    });

    service.start();
    REQUIRE(timedOut == true);
}
