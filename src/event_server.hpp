#pragma once

#include <fcntl.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <functional>
#include <unordered_set>
#include <vector>

namespace event_server {

inline int set_nonblock(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

inline void close_all_clients(std::unordered_set<int>& clients) {
    for (int fd : clients) {
        ::close(fd);
    }
    clients.clear();
}

inline void accept_all_nonblocking(
    std::atomic<bool>& running,
    int listen_fd,
    std::unordered_set<int>& clients,
    const std::function<bool(int)>& prepare_client_fn
) {
    while (running.load()) {
        int cfd = ::accept(listen_fd, nullptr, nullptr);
        if (cfd < 0) {
            break;
        }
        if (prepare_client_fn(cfd)) {
            clients.insert(cfd);
        } else {
            ::close(cfd);
        }
    }
}

inline void echo_once_nonblocking(int fd, std::unordered_set<int>& clients, std::vector<char>& buffer) {
    ssize_t n = ::recv(fd, buffer.data(), buffer.size(), 0);
    if (n <= 0) {
        ::close(fd);
        clients.erase(fd);
        return;
    }

    ssize_t sent = 0;
    while (sent < n) {
        ssize_t s = ::send(fd, buffer.data() + sent, static_cast<size_t>(n - sent), 0);
        if (s < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            ::close(fd);
            clients.erase(fd);
            return;
        }
        sent += s;
    }
}

inline void run_select_loop(
    std::atomic<bool>& running,
    int listen_fd,
    std::unordered_set<int>& clients,
    const std::function<void()>& accept_fn,
    const std::function<void(int)>& echo_fn
) {
    while (running.load()) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listen_fd, &readfds);
        int max_fd = listen_fd;

        for (int fd : clients) {
            FD_SET(fd, &readfds);
            max_fd = std::max(max_fd, fd);
        }

        timeval timeout{0, 200000};
        int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
        if (ready <= 0) {
            continue;
        }

        if (FD_ISSET(listen_fd, &readfds)) {
            accept_fn();
        }

        std::vector<int> active(clients.begin(), clients.end());
        for (int fd : active) {
            if (FD_ISSET(fd, &readfds)) {
                echo_fn(fd);
            }
        }
    }
}

inline void run_poll_loop(
    std::atomic<bool>& running,
    int listen_fd,
    std::unordered_set<int>& clients,
    const std::function<void()>& accept_fn,
    const std::function<void(int)>& echo_fn
) {
    while (running.load()) {
        std::vector<pollfd> fds;
        fds.reserve(clients.size() + 1);
        fds.push_back({listen_fd, POLLIN, 0});
        for (int fd : clients) {
            fds.push_back({fd, POLLIN, 0});
        }

        int ready = ::poll(fds.data(), fds.size(), 200);
        if (ready <= 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            accept_fn();
        }

        for (size_t i = 1; i < fds.size(); ++i) {
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                echo_fn(fds[i].fd);
            }
        }
    }
}

}  // namespace event_server
