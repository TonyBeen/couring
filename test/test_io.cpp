#include "catch.hpp"
#include "error_code.h"
#include "uring_service.h"
#include "io/co_file.h"
#include "io/co_socket.h"

#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>

TEST_CASE("ErrorCode and strerror lookup", "[error_code]") {
    using namespace couring;

    ErrorCode ec_ok;
    REQUIRE(!ec_ok);
    REQUIRE(ec_ok.value() == 0);
    REQUIRE(std::string(ec_ok.message()) == "Success");

    ErrorCode ec_cancel(Errc::FiberCanceled);
    REQUIRE(static_cast<bool>(ec_cancel));
    REQUIRE(ec_cancel.value() == 1005);
    REQUIRE(std::string(ec_cancel.message()) == "Fiber operation was canceled");

    const char* str_sys = couring_strerror(-ENOENT);
    REQUIRE(str_sys != nullptr);
    REQUIRE(std::string(str_sys) == strerror(ENOENT));

    const char* str_custom = couring_strerror(-1003);
    REQUIRE(str_custom != nullptr);
    REQUIRE(std::string(str_custom) == "Submission queue is full");
}

TEST_CASE("Coroutine File I/O (co_write_file & co_read_file)", "[io_file]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    const char* testPath = "/tmp/couring_test_file.tmp";
    unlink(testPath);

    int32_t fd = open(testPath, O_CREAT | O_RDWR | O_TRUNC, 0644);
    REQUIRE(fd >= 0);

    const std::string testData = "Hello io_uring and Coroutines in C++!";

    service.schedule([&]() noexcept {
        ssize_t written = co_write_file(fd, testData.data(), testData.size(), 0);
        REQUIRE(written == static_cast<ssize_t>(testData.size()));

        char readBuf[128] = {0};
        ssize_t nread = co_read_file(fd, readBuf, sizeof(readBuf), 0);
        REQUIRE(nread == static_cast<ssize_t>(testData.size()));
        REQUIRE(std::string(readBuf, nread) == testData);

        co_close(fd);
        service.stop();
    });

    service.start();
    unlink(testPath);
}

TEST_CASE("Coroutine Socket TCP Echo (co_accept, co_connect, co_send, co_recv)", "[io_socket]") {
    using namespace eular;
    using namespace couring;

    UringService service(128);
    REQUIRE(service.isValid());

    int32_t listenfd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listenfd >= 0);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddr.sin_port = 0; // Let OS assign port

    int32_t bres = bind(listenfd, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr));
    REQUIRE(bres == 0);

    socklen_t addrLen = sizeof(serverAddr);
    getsockname(listenfd, reinterpret_cast<sockaddr*>(&serverAddr), &addrLen);
    uint16_t port = ntohs(serverAddr.sin_port);

    int32_t lres = listen(listenfd, 5);
    REQUIRE(lres == 0);

    const std::string msg = "Echo from couring coroutine!";
    std::string receivedMsg;

    // Server coroutine
    service.schedule([&]() noexcept {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int32_t clientFd = co_accept(listenfd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        REQUIRE(clientFd >= 0);

        char buf[128] = {0};
        ssize_t n = co_recv(clientFd, buf, sizeof(buf));
        REQUIRE(n > 0);

        // Echo back
        ssize_t sent = co_send(clientFd, buf, n);
        REQUIRE(sent == n);

        co_close(clientFd);
    });

    // Client coroutine
    service.schedule([&]() noexcept {
        int32_t connFd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(connFd >= 0);

        sockaddr_in saddr{};
        saddr.sin_family = AF_INET;
        saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        saddr.sin_port = htons(port);

        int32_t cres = co_connect(connFd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
        REQUIRE(cres == 0);

        ssize_t sent = co_send(connFd, msg.data(), msg.size());
        REQUIRE(sent == static_cast<ssize_t>(msg.size()));

        char recvBuf[128] = {0};
        ssize_t n = co_recv(connFd, recvBuf, sizeof(recvBuf));
        REQUIRE(n == static_cast<ssize_t>(msg.size()));
        receivedMsg.assign(recvBuf, n);

        co_close(connFd);
        co_close(listenfd);
        service.stop();
    });

    service.start();

    REQUIRE(receivedMsg == msg);
}

TEST_CASE("Coroutine Socket Timeout and Cancellation (co_recv timeout)", "[io_socket_timeout]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    int sv[2];
    int res = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    REQUIRE(res == 0);

    bool timedOut = false;
    service.schedule([&]() noexcept {
        char buf[64] = {0};
        // Expect timeout in 50ms since sv[1] sends nothing
        ssize_t ret = co_recv(sv[0], buf, sizeof(buf), 0, 50);
        if (ret == -ETIMEDOUT) {
            timedOut = true;
        }
        co_close(sv[0]);
        co_close(sv[1]);
        service.stop();
    });

    service.start();
    REQUIRE(timedOut == true);
}

TEST_CASE("Coroutine UDP datagrams (co_sendto, co_recvfrom)", "[io_socket_udp]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    int32_t s1 = socket(AF_INET, SOCK_DGRAM, 0);
    int32_t s2 = socket(AF_INET, SOCK_DGRAM, 0);
    REQUIRE(s1 >= 0);
    REQUIRE(s2 >= 0);

    sockaddr_in a1{}, a2{};
    a1.sin_family = AF_INET;
    a1.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a1.sin_port = 0;

    a2.sin_family = AF_INET;
    a2.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a2.sin_port = 0;

    REQUIRE(bind(s1, reinterpret_cast<sockaddr*>(&a1), sizeof(a1)) == 0);
    REQUIRE(bind(s2, reinterpret_cast<sockaddr*>(&a2), sizeof(a2)) == 0);

    socklen_t l1 = sizeof(a1), l2 = sizeof(a2);
    getsockname(s1, reinterpret_cast<sockaddr*>(&a1), &l1);
    getsockname(s2, reinterpret_cast<sockaddr*>(&a2), &l2);

    const std::string udpMsg = "UDP datagram test through io_uring";
    std::string received;

    service.schedule([&]() noexcept {
        // Send from s1 to s2
        ssize_t sent = co_sendto(s1, udpMsg.data(), udpMsg.size(), 0, reinterpret_cast<sockaddr*>(&a2), sizeof(a2));
        REQUIRE(sent == static_cast<ssize_t>(udpMsg.size()));

        sockaddr_in srcAddr{};
        socklen_t srcLen = sizeof(srcAddr);
        char buf[128] = {0};
        ssize_t n = co_recvfrom(s2, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&srcAddr), &srcLen, 500);
        REQUIRE(n == static_cast<ssize_t>(udpMsg.size()));
        received.assign(buf, n);
        REQUIRE(srcAddr.sin_port == a1.sin_port);

        co_close(s1);
        co_close(s2);
        service.stop();
    });

    service.start();
    REQUIRE(received == udpMsg);
}

TEST_CASE("Coroutine Socket scatter-gather and batch (co_sendmsg, co_recvmsg, co_sendmmsg, co_recvmmsg)", "[io_socket_msg]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0);

    service.schedule([&]() noexcept {
        // Test co_sendmsg / co_recvmsg
        std::string part1 = "Hello ";
        std::string part2 = "World!";
        struct iovec iov[2];
        iov[0].iov_base = const_cast<char*>(part1.data());
        iov[0].iov_len = part1.size();
        iov[1].iov_base = const_cast<char*>(part2.data());
        iov[1].iov_len = part2.size();

        struct msghdr msg{};
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;

        ssize_t sent = co_sendmsg(sv[0], &msg, 0);
        REQUIRE(sent == static_cast<ssize_t>(part1.size() + part2.size()));

        char recvBuf[64] = {0};
        struct iovec r_iov;
        r_iov.iov_base = recvBuf;
        r_iov.iov_len = sizeof(recvBuf);

        struct msghdr r_msg{};
        r_msg.msg_iov = &r_iov;
        r_msg.msg_iovlen = 1;

        ssize_t n = co_recvmsg(sv[1], &r_msg, 0, 500);
        REQUIRE(n == static_cast<ssize_t>(part1.size() + part2.size()));
        REQUIRE(std::string(recvBuf, n) == "Hello World!");

        // Test co_sendmmsg / co_recvmmsg
        struct mmsghdr sendvec[2]{};
        std::string m1 = "batch_msg_1";
        std::string m2 = "batch_msg_2";
        struct iovec s_iov1{const_cast<char*>(m1.data()), m1.size()};
        struct iovec s_iov2{const_cast<char*>(m2.data()), m2.size()};
        sendvec[0].msg_hdr.msg_iov = &s_iov1;
        sendvec[0].msg_hdr.msg_iovlen = 1;
        sendvec[1].msg_hdr.msg_iov = &s_iov2;
        sendvec[1].msg_hdr.msg_iovlen = 1;

        int32_t s_batch = co_sendmmsg(sv[0], sendvec, 2, 0);
        REQUIRE(s_batch == 2);

        char rb1[32] = {0}, rb2[32] = {0};
        struct mmsghdr recvvec[2]{};
        struct iovec r_iov1{rb1, sizeof(rb1)};
        struct iovec r_iov2{rb2, sizeof(rb2)};
        recvvec[0].msg_hdr.msg_iov = &r_iov1;
        recvvec[0].msg_hdr.msg_iovlen = 1;
        recvvec[1].msg_hdr.msg_iov = &r_iov2;
        recvvec[1].msg_hdr.msg_iovlen = 1;

        int32_t r_batch = co_recvmmsg(sv[1], recvvec, 2, 0);
        REQUIRE(r_batch == 2);
        REQUIRE(std::string(rb1, recvvec[0].msg_len) == m1);
        REQUIRE(std::string(rb2, recvvec[1].msg_len) == m2);

        co_close(sv[0]);
        co_close(sv[1]);
        service.stop();
    });

    service.start();
}

TEST_CASE("Coroutine Socket accept4 and shutdown", "[io_socket_misc]") {
    using namespace eular;
    using namespace couring;

    UringService service(64);
    REQUIRE(service.isValid());

    int32_t listenfd = socket(AF_INET, SOCK_STREAM, 0);
    REQUIRE(listenfd >= 0);

    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    saddr.sin_port = 0;
    REQUIRE(bind(listenfd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) == 0);
    REQUIRE(listen(listenfd, 5) == 0);

    socklen_t slen = sizeof(saddr);
    getsockname(listenfd, reinterpret_cast<sockaddr*>(&saddr), &slen);

    service.schedule([&]() noexcept {
        sockaddr_in caddr{};
        socklen_t clen = sizeof(caddr);
        int32_t cfd = co_accept4(listenfd, reinterpret_cast<sockaddr*>(&caddr), &clen, SOCK_NONBLOCK);
        REQUIRE(cfd >= 0);

        int flags = fcntl(cfd, F_GETFL, 0);
        REQUIRE((flags & O_NONBLOCK) != 0);

        int32_t s_ret = co_shutdown(cfd, SHUT_RDWR);
        REQUIRE(s_ret == 0);

        co_close(cfd);
    });

    service.schedule([&]() noexcept {
        int32_t client = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(client >= 0);
        int32_t cres = co_connect(client, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr));
        REQUIRE(cres == 0);

        co_close(client);
        co_close(listenfd);
        service.stop();
    });

    service.start();
}
