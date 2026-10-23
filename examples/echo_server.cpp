/*************************************************************************
    > File Name: examples/echo_server.cpp
    > Author: hsz
    > Brief: High-performance coroutine TCP echo server using couring
    > Created Time: 2026-10-23
 ************************************************************************/

#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "uring_service.h"
#include "fiber.h"
#include "logger.h"
#include "io/co_socket.h"

static eular::UringService* g_service = nullptr;

static void handle_signal(int sig)
{
    (void)sig;
    if (g_service) {
        const char msg[] = "\nReceived signal, stopping server...\n";
        ssize_t n = ::write(STDOUT_FILENO, msg, sizeof(msg) - 1);
        (void)n;
        g_service->stop();
    }
}

static void handle_client(int32_t client_fd) noexcept
{
    char buf[4096];
    while (true) {
        // Read with 60-second idle timeout
        ssize_t n = couring::co_recv(client_fd, buf, sizeof(buf), 0, 60000);
        if (n <= 0) {
            break; // EOF, error, or timeout
        }

        // Echo back
        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = couring::co_send(client_fd, buf + sent, n - sent, 0, 10000);
            if (s <= 0) {
                break;
            }
            sent += s;
        }

        if (sent < n) {
            break;
        }
    }

    couring::co_close(client_fd);
}

int main(int argc, char* argv[])
{
    uint16_t port = 8080;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::atoi(argv[1]));
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    signal(SIGPIPE, SIG_IGN);

    couring::set_log_level(couring::LogLevel::INFO);

    eular::UringService service(1024);
    if (!service.isValid()) {
        std::cerr << "Failed to initialize io_uring service\n";
        return 1;
    }
    g_service = &service;

    int32_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "Failed to bind to port " << port << ": " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    if (listen(listen_fd, 1024) != 0) {
        std::cerr << "Failed to listen on socket: " << strerror(errno) << "\n";
        close(listen_fd);
        return 1;
    }

    std::cout << "couring Echo Server listening on 0.0.0.0:" << port << "...\n";

    // Server accept loop coroutine
    service.schedule([listen_fd, &service]() noexcept {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);

            int32_t client_fd = couring::co_accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
            if (client_fd < 0) {
                break;
            }

            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));

            // Spawn coroutine for each client
            service.schedule([client_fd]() noexcept {
                handle_client(client_fd);
            });
        }

        couring::co_close(listen_fd);
    });

    service.start();
    std::cout << "Server stopped gracefully.\n";
    return 0;
}
