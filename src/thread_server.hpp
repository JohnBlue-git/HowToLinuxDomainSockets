#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <vector>

namespace thread_server {

inline void run_thread_per_client(
    std::atomic<bool>& running,
    const std::function<int()>& accept_fn,
    const std::function<void(int)>& handle_client_fn
) {
    std::vector<std::thread> workers;
    workers.reserve(2048);

    while (running.load()) {
        int client_fd = accept_fn();
        if (client_fd < 0) {
            if (!running.load()) {
                break;
            }
            continue;
        }
        workers.emplace_back(handle_client_fn, client_fd);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

}  // namespace thread_server
