#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "thread_server.hpp"

namespace {
std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

int create_server(const std::string& path) {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error("socket(AF_UNIX) failed");
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        ::close(fd);
        throw std::runtime_error("socket path too long");
    }

    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    ::unlink(path.c_str());

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(fd);
        throw std::runtime_error("bind(AF_UNIX) failed");
    }

    if (::listen(fd, 1024) < 0) {
        ::close(fd);
        throw std::runtime_error("listen(AF_UNIX) failed");
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
    std::string socket_path = "/tmp/uds_echo.sock";

    if (argc >= 2) {
        socket_path = argv[1];
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        int server_fd = create_server(socket_path);
        std::cout << "unix domain thread server listening on " << socket_path << std::endl;

        auto accept_fn = [server_fd]() { return ::accept(server_fd, nullptr, nullptr); };
        thread_server::run_thread_per_client(running, accept_fn, handle_client);

        ::close(server_fd);
        ::unlink(socket_path.c_str());
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
