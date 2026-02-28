#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/epoll.h>
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#include <sys/time.h>
#endif

#include <algorithm>
#include <atomic>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "event_server.hpp"

namespace {
std::atomic<bool> running{true};

enum class Mode {
    Select,
    Poll,
    Epoll,
    Kqueue
};

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
        throw std::runtime_error("setsockopt() failed");
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
    if (event_server::set_nonblock(fd) < 0) {
        ::close(fd);
        throw std::runtime_error("set_nonblock(listen_fd) failed");
    }
    return fd;
}

void close_all(std::unordered_set<int>& clients) {
    event_server::close_all_clients(clients);
}

void accept_all(int listen_fd, std::unordered_set<int>& clients) {
    auto prepare_client = [](int cfd) {
        return event_server::set_nonblock(cfd) == 0;
    };
    event_server::accept_all_nonblocking(running, listen_fd, clients, prepare_client);
}

void echo_once(int fd, std::unordered_set<int>& clients, std::vector<char>& buf) {
    event_server::echo_once_nonblocking(fd, clients, buf);
}

void run_select(int listen_fd) {
    std::unordered_set<int> clients;
    std::vector<char> buffer(4096);

    auto accept_fn = [&]() { accept_all(listen_fd, clients); };
    auto echo_fn = [&](int fd) { echo_once(fd, clients, buffer); };
    event_server::run_select_loop(running, listen_fd, clients, accept_fn, echo_fn);

    close_all(clients);
}

void run_poll(int listen_fd) {
    std::unordered_set<int> clients;
    std::vector<char> buffer(4096);

    auto accept_fn = [&]() { accept_all(listen_fd, clients); };
    auto echo_fn = [&](int fd) { echo_once(fd, clients, buffer); };
    event_server::run_poll_loop(running, listen_fd, clients, accept_fn, echo_fn);

    close_all(clients);
}

#if defined(__linux__)
void run_epoll(int listen_fd) {
    int epfd = ::epoll_create1(0);
    if (epfd < 0) {
        throw std::runtime_error("epoll_create1() failed");
    }

    std::unordered_set<int> clients;
    std::vector<char> buffer(4096);

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = listen_fd;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) < 0) {
        ::close(epfd);
        throw std::runtime_error("epoll_ctl ADD listen_fd failed");
    }

    std::vector<epoll_event> events(1024);

    while (running.load()) {
        int n = ::epoll_wait(epfd, events.data(), static_cast<int>(events.size()), 200);
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd) {
                while (running.load()) {
                    int cfd = ::accept(listen_fd, nullptr, nullptr);
                    if (cfd < 0) {
                        break;
                    }
                    if (event_server::set_nonblock(cfd) < 0) {
                        ::close(cfd);
                        continue;
                    }
                    clients.insert(cfd);
                    epoll_event cev{};
                    cev.events = EPOLLIN;
                    cev.data.fd = cfd;
                    ::epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                }
            } else {
                if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                    ::close(fd);
                    clients.erase(fd);
                    continue;
                }
                echo_once(fd, clients, buffer);
                if (!clients.contains(fd)) {
                    ::epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                }
            }
        }
    }

    close_all(clients);
    ::close(epfd);
}
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
void run_kqueue(int listen_fd) {
    int kq = ::kqueue();
    if (kq < 0) {
        throw std::runtime_error("kqueue() failed");
    }

    std::unordered_set<int> clients;
    std::vector<char> buffer(4096);

    struct kevent change;
    EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
    if (::kevent(kq, &change, 1, nullptr, 0, nullptr) < 0) {
        ::close(kq);
        throw std::runtime_error("kevent add listen failed");
    }

    std::vector<struct kevent> events(1024);

    while (running.load()) {
        timespec ts{0, 200000000};
        int n = ::kevent(kq, nullptr, 0, events.data(), static_cast<int>(events.size()), &ts);
        if (n <= 0) {
            continue;
        }

        for (int i = 0; i < n; ++i) {
            int fd = static_cast<int>(events[i].ident);
            if (fd == listen_fd) {
                while (running.load()) {
                    int cfd = ::accept(listen_fd, nullptr, nullptr);
                    if (cfd < 0) {
                        break;
                    }
                    if (event_server::set_nonblock(cfd) < 0) {
                        ::close(cfd);
                        continue;
                    }
                    clients.insert(cfd);
                    struct kevent add_client;
                    EV_SET(&add_client, cfd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
                    ::kevent(kq, &add_client, 1, nullptr, 0, nullptr);
                }
            } else {
                echo_once(fd, clients, buffer);
                if (!clients.contains(fd)) {
                    struct kevent del_client;
                    EV_SET(&del_client, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
                    ::kevent(kq, &del_client, 1, nullptr, 0, nullptr);
                }
            }
        }
    }

    close_all(clients);
    ::close(kq);
}
#endif

Mode parse_mode(const std::string& mode) {
    if (mode == "select") {
        return Mode::Select;
    }
    if (mode == "poll") {
        return Mode::Poll;
    }
    if (mode == "epoll") {
        return Mode::Epoll;
    }
    if (mode == "kqueue") {
        return Mode::Kqueue;
    }
    throw std::invalid_argument("unknown mode: " + mode);
}
}  // namespace

int main(int argc, char** argv) {
    uint16_t port = 9001;
    std::string mode_name = "epoll";

    if (argc >= 2) {
        mode_name = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        auto mode = parse_mode(mode_name);
        int listen_fd = create_listen_socket(port);
        std::cout << "event-driven server mode=" << mode_name << " listening on 0.0.0.0:" << port << std::endl;

        switch (mode) {
            case Mode::Select:
                run_select(listen_fd);
                break;
            case Mode::Poll:
                run_poll(listen_fd);
                break;
            case Mode::Epoll:
#if defined(__linux__)
                run_epoll(listen_fd);
#else
                throw std::runtime_error("epoll is not available on this platform");
#endif
                break;
            case Mode::Kqueue:
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
                run_kqueue(listen_fd);
#else
                throw std::runtime_error("kqueue is not available on this platform");
#endif
                break;
        }

        ::close(listen_fd);
    } catch (const std::exception& ex) {
        std::cerr << "fatal: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
