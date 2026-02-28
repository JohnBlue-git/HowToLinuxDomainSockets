#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "thread_server.hpp"

namespace {
std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

int create_listen_socket(uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket() failed");
    }

    int enable = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        ::close(fd);
        throw std::runtime_error("setsockopt(SO_REUSEADDR) failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind() failed");
    }

    if (::listen(fd, 1024) < 0) {
        ::close(fd);
        throw std::runtime_error("listen() failed");
    }

    return fd;
}

void handle_client(int client_fd) {
    std::vector<char> buffer(4096);
    while (running.load()) {
        ssize_t n = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (n <= 0) {
            break;
        }

        ssize_t sent = 0;
        while (sent < n) {
            ssize_t s = ::send(client_fd, buffer.data() + sent, static_cast<size_t>(n - sent), 0);
            if (s <= 0) {
                ::close(client_fd);
                return;
            }
            sent += s;
        }
    }
    ::close(client_fd);
}
}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 9000;
    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        int listen_fd = create_listen_socket(port);
        std::cout << "thread-per-client server listening on 0.0.0.0:" << port << std::endl;

        auto accept_fn = [listen_fd]() {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            return ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        };

        thread_server::run_thread_per_client(running, accept_fn, handle_client);

        ::close(listen_fd);
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
