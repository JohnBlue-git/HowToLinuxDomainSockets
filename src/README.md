# `src/` Code Structure Guide

This document explains the structure of each C++ source file and highlights the key code sections where design approaches differ.

## 1) File Map

- `tcp_thread_server.cpp`: TCP echo server using **thread-per-client**.
- `tcp_event_server.cpp`: TCP echo server using **event-driven I/O** (`select`, `poll`, `epoll`, `kqueue`).
- `unix_domain_thread_server.cpp`: Unix Domain Socket thread-per-client server (`thread`).
- `unix_domain_event_server.cpp`: Unix Domain Socket event-driven server (`select`, `poll`, `epoll`, `kqueue`).
- `tcp_client.cpp`: TCP test client.
- `unix_domain_client.cpp`: Unix Domain Socket test client.
- `thread_server.hpp`: Shared helpers for thread-per-client server loops.
- `event_server.hpp`: Shared helpers for non-blocking/event-driven server loops.

---

## 2) `tcp_thread_server.cpp`

### Structure

1. Global state and signal handling
   - `std::atomic<bool> running`
   - `signal_handler(...)`
2. Socket setup
   - `create_listen_socket(port)`
3. Per-connection worker
   - `handle_client(client_fd)`
4. Main loop
   - `accept(...)` + `workers.emplace_back(handle_client, client_fd)`

### Key Differentiator

The core difference is **one thread per accepted connection**:

```cpp
while (running.load()) {
    int client_fd = ::accept(listen_fd, ...);
    if (client_fd < 0) { ... }
    workers.emplace_back(handle_client, client_fd);
}
```

This is simple to reason about, but memory/context-switch overhead grows with connection count.

---

## 3) `tcp_event_server.cpp`

### Structure

1. Mode abstraction
   - `enum class Mode { Select, Poll, Epoll, Kqueue }`
   - `parse_mode(...)`
2. Shared helpers
   - `set_nonblock(fd)`
   - `create_listen_socket(port)`
   - `accept_all(...)`
   - `echo_once(...)`
   - `close_all(...)`
3. Backend-specific event loops
   - `run_select(...)`
   - `run_poll(...)`
   - `run_epoll(...)` (Linux)
   - `run_kqueue(...)` (BSD/macOS)
4. Main dispatch
   - `switch (mode) { ... }`

### Key Differentiators by Backend

#### A) `select`

Rebuilds `fd_set` every iteration and scans descriptors up to `max_fd`:

```cpp
FD_ZERO(&readfds);
FD_SET(listen_fd, &readfds);
for (int fd : clients) {
    FD_SET(fd, &readfds);
}
int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
```

#### B) `poll`

Builds `std::vector<pollfd>` and scans `revents` linearly:

```cpp
std::vector<pollfd> fds;
fds.push_back({listen_fd, POLLIN, 0});
for (int fd : clients) {
    fds.push_back({fd, POLLIN, 0});
}
int ready = ::poll(fds.data(), fds.size(), 200);
```

#### C) `epoll` (Linux)

Registers fd interest once via `epoll_ctl`, then waits for ready events:

```cpp
int epfd = ::epoll_create1(0);
::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
int n = ::epoll_wait(epfd, events.data(), events.size(), 200);
```

#### D) `kqueue` (BSD/macOS)

Registers event filters and consumes ready events from the kernel queue:

```cpp
int kq = ::kqueue();
EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
::kevent(kq, &change, 1, nullptr, 0, nullptr);
int n = ::kevent(kq, nullptr, 0, events.data(), events.size(), &ts);
```

### Common Event-Driven Behavior

All backends reuse the same per-socket data path:

- `accept_all(...)` for new connections
- `echo_once(...)` for non-blocking read/write and disconnect cleanup

This keeps backend differences focused only on readiness notification.

---

## 4) `unix_domain_thread_server.cpp`

### Structure

1. Global state and signal handling
2. Unix socket setup in `create_server(path)`
3. Shared thread helper usage from `thread_server.hpp`
   - `thread_server::run_thread_per_client(...)`
4. Cleanup with `::unlink(socket_path.c_str())`

### Key Differentiator

Uses `AF_UNIX` and filesystem path (`sockaddr_un::sun_path`) instead of IP/port:

```cpp
int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
sockaddr_un addr{};
addr.sun_family = AF_UNIX;
std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

This is optimized for same-host IPC and does not cross machine boundaries.

### Key Sections Where Approaches Differ

#### A) Thread-per-client

```cpp
while (running.load()) {
   int client_fd = ::accept(server_fd, nullptr, nullptr);
   ...
   workers.emplace_back(handle_client, client_fd);
}
```

## 5) `unix_domain_event_server.cpp`

### Structure

1. Mode abstraction and parser
   - `enum class Mode { Select, Poll, Epoll, Kqueue }`
2. Unix socket setup in `create_server(path)`
3. Shared event helper usage from `event_server.hpp`
   - `set_nonblock`, `accept_all_nonblocking`, `echo_once_nonblocking`
   - `run_select_loop`, `run_poll_loop`
4. Backend-specific implementations
   - `run_epoll(...)` (Linux)
   - `run_kqueue(...)` (BSD/macOS)
5. Main dispatch + cleanup

### Key Sections Where Approaches Differ

#### B) Event-driven `select` / `poll`

```cpp
int ready = ::select(max_fd + 1, &readfds, nullptr, nullptr, &timeout);
// or
int ready = ::poll(fds.data(), fds.size(), 200);
```

#### C) Event-driven `epoll` (Linux)

```cpp
int epfd = ::epoll_create1(0);
::epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev);
int n = ::epoll_wait(epfd, events.data(), events.size(), 200);
```

#### D) Event-driven `kqueue` (BSD/macOS)

```cpp
int kq = ::kqueue();
EV_SET(&change, listen_fd, EVFILT_READ, EV_ADD, 0, 0, nullptr);
int n = ::kevent(kq, nullptr, 0, events.data(), events.size(), &ts);
```

#### CLI Mode Selection

```cpp
./bin/unix_domain_thread_server <socket_path>
./bin/unix_domain_event_server <select|poll|epoll|kqueue> <socket_path>
```

---

## 6) `tcp_client.cpp`

### Structure

1. Parse args: `host`, `port`, `repeat`, `payload`
2. Connect via `AF_INET`
3. Loop `repeat` times: send payload, then recv until full echo length
4. Print `ok` on success

### Purpose

A minimal functional client for manual testing and quick smoke checks.

---

## 7) `unix_domain_client.cpp`

### Structure

1. Parse args: `socket_path`, `repeat`, `payload`
2. Connect via `AF_UNIX`
3. Loop `repeat` times: send payload, recv full echo
4. Print `ok` on success

### Purpose

Companion client for Unix Domain servers, mirroring TCP client behavior with a local socket path.

---

## 8) Quick Comparison of Core Approach Differences

- **Connection model**
  - Thread server: creates a dedicated thread per connection.
  - Event server: manages many connections in one event loop.

- **Readiness mechanism**
   - Thread server: synchronous per-thread I/O.
  - Event server: non-blocking sockets + readiness APIs (`select/poll/epoll/kqueue`).

- **Address family**
  - TCP server/client: `AF_INET` + IP/port.
  - Unix server/client: `AF_UNIX` + socket file path.

- **Scalability profile**
  - Thread-per-client: simpler control flow, higher overhead at high concurrency.
  - Event-driven: more complex control flow, usually better high-concurrency efficiency.

---

## 9) Shared Header Files (`.hpp`)

This project uses two shared header files to avoid duplicated server-loop logic across TCP and Unix Domain implementations.

### `thread_server.hpp`

Purpose:

- Provides reusable thread-per-client loop helper.
- Used by both `tcp_thread_server.cpp` and `unix_domain_thread_server.cpp`.

Main API:

- `thread_server::run_thread_per_client(...)`
   - Inputs:
      - `std::atomic<bool>& running`
      - `accept_fn` (`std::function<int()>`)
      - `handle_client_fn` (`std::function<void(int)>`)
   - Behavior:
      - Repeatedly calls `accept_fn()`.
      - Spawns one worker thread per accepted client.
      - Joins all worker threads during shutdown.

### `event_server.hpp`

Purpose:

- Provides shared non-blocking/event-driven helpers for both TCP and Unix Domain event servers.
- Keeps `select`/`poll` loop structure and low-level fd utilities in one place.

Main APIs:

- `event_server::set_nonblock(fd)`
   - Enables non-blocking mode on a file descriptor.

- `event_server::close_all_clients(clients)`
   - Closes and clears tracked client descriptors.

- `event_server::accept_all_nonblocking(...)`
   - Accepts all currently available clients from a non-blocking listen socket.

- `event_server::echo_once_nonblocking(...)`
   - Performs one non-blocking recv/send echo cycle with disconnect/error cleanup.

- `event_server::run_select_loop(...)`
   - Shared `select` event loop skeleton.

- `event_server::run_poll_loop(...)`
   - Shared `poll` event loop skeleton.
