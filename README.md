# HowToLinuxDomainSockets

This project provides 3 C++ socket server designs and uses `pytest` to measure performance (throughput) and memory usage (peak RSS):

- Thread-Per-Client Design with TCP socket
- Event-Driven Design with TCP socket (`select` / `poll` / `epoll` / `kqueue`)
- Linux Unix Domain Socket design (supports `thread` / `select` / `poll` / `epoll` / `kqueue`)

## 1) Project Structure

- `src/tcp_thread_server.cpp`: Thread-per-client TCP echo server
- `src/tcp_event_server.cpp`: Event-driven TCP echo server (switchable: `select`, `poll`, `epoll`, `kqueue`)
- `src/unix_domain_thread_server.cpp`: Unix Domain Socket thread-per-client server (`AF_UNIX`)
- `src/unix_domain_event_server.cpp`: Unix Domain Socket event-driven server (`AF_UNIX`)
- `src/thread_server.hpp`: Shared thread-per-client server loop helpers
- `src/event_server.hpp`: Shared event-driven helper functions
- `src/tcp_client.cpp`: TCP echo client
- `src/unix_domain_client.cpp`: Unix Domain Socket echo client
- `tests/test_benchmarks.py`: `pytest` load tests and metrics (speed + memory)
- `tests/conftest.py`: Print benchmark grid summary at the end of pytest

## 2) Build

```bash
make
```

Binaries are generated in `bin/`.

## 3) Manual Run Examples

### Thread-Per-Client TCP

```bash
./bin/tcp_thread_server 9000
./bin/tcp_client 127.0.0.1 9000 1000 ping
```

### Event-Driven TCP

```bash
./bin/tcp_event_server select 9001
./bin/tcp_event_server poll 9001
./bin/tcp_event_server epoll 9001
./bin/tcp_event_server kqueue 9001
./bin/tcp_client 127.0.0.1 9001 1000 ping
```

> Note:
> - `epoll` is only available on Linux.
> - `kqueue` is only available on BSD/macOS (Linux will report it as unavailable).

### Unix Domain Socket

In this repository, Unix Domain Socket servers are split into two binaries:

- `unix_domain_thread_server`: thread-per-client implementation
- `unix_domain_event_server`: event-driven implementation (`select` / `poll` / `epoll` / `kqueue`)

```bash
./bin/unix_domain_thread_server /tmp/uds_echo.sock
./bin/unix_domain_event_server select /tmp/uds_echo.sock
./bin/unix_domain_event_server poll /tmp/uds_echo.sock
./bin/unix_domain_event_server epoll /tmp/uds_echo.sock
./bin/unix_domain_event_server kqueue /tmp/uds_echo.sock
./bin/unix_domain_client /tmp/uds_echo.sock 1000 ping
```

## 4) Measure Speed and Memory with pytest

```bash
pytest -q
```

Test flow:

1. Automatically run `make clean && make`
2. Start the target server
3. Run multi-threaded client load (default: 40 clients x 300 loops)
4. Measure:
	- Throughput: `throughput_msg_per_sec`
	- Peak memory: `VmRSS` from `/proc/<pid>/status` (KB)
5. Print a grid summary in terminal at test end (no JSON files generated)
6. Unix Domain benchmark compares **event-driven poll + epoll** modes (`unix_domain_socket_poll`, `unix_domain_socket_epoll`)

## 5) Current Test Results (Local Linux / Single Run Snapshot)

Test parameters: `clients=40`, `loops_per_client=300`, `payload_size=64`

> These numbers are one run snapshot. Throughput and elapsed time can vary across CPU load, kernel scheduling, and background processes.

What "workload-dependent" means here:

- Benchmark ranking can change when connection count, message size, request rate, CPU contention, or kernel/network state changes.
- A mode that is faster at `40 x 300 x 64B` may not be fastest at `400 x 300 x 4KB`.
- Always compare modes under your production-like traffic pattern before deciding.

| scenario | throughput(msg/s) | peak_rss_kb | elapsed_sec | clients | loops | payload |
|---|---:|---:|---:|---:|---:|---:|
| thread_per_client_tcp | 24695.96 | 4096 | 0.4859 | 40 | 300 | 64 |
| event_driven_tcp_select | 38936.85 | 3456 | 0.3082 | 40 | 300 | 64 |
| event_driven_tcp_poll | 42394.78 | 3328 | 0.2831 | 40 | 300 | 64 |
| event_driven_tcp_epoll | 37428.67 | 3328 | 0.3206 | 40 | 300 | 64 |
| unix_domain_socket_poll | 52995.45 | 3328 | 0.2264 | 40 | 300 | 64 |
| unix_domain_socket_epoll | 34135.63 | 3328 | 0.3515 | 40 | 300 | 64 |

### Test Result Field Definitions

- `scenario`: Benchmark scenario name (maps to server design or event mode).
- `throughput(msg/s)`: Number of successful request/echo operations per second (higher is better).
- `peak_rss_kb`: Peak resident memory of the server during the test (KB, lower is better).
- `elapsed_sec`: Total elapsed time for the scenario in seconds (lower is better).
- `clients`: Number of concurrent client connections.
- `loops`: Number of requests sent by each client.
- `payload`: Payload size per request (bytes).

> How to read this table:
> - For speed comparison, prioritize `throughput(msg/s)` and `elapsed_sec`.
> - For resource efficiency, focus on `peak_rss_kb`.
> - Keep `clients/loops/payload` fixed for fair comparisons.

## 6) Design Notes and Comparison

### A. Thread-Per-Client (TCP)

Characteristics:

- Pros: Straightforward implementation, clear per-client logic.
- Cons: One thread per connection under high concurrency increases context-switch and stack-memory overhead.
- Best for: Small-to-medium connection counts with more complex per-client logic and easy maintenance needs.

### B. Event-Driven (TCP)

Characteristics:

- Pros: Single/few threads can manage many connections with more stable memory usage.
- Cons: State-machine style flow is more complex to implement and debug.
- Best for: High-concurrency I/O services.

### C. Unix Domain Socket (AF_UNIX)

Characteristics:

- Pros: Intra-host communication is often faster than loopback TCP with lower overhead.
- Cons: Not usable across hosts; socket-file path management is required.
- Best for: Service decomposition on the same host, sidecars, and local proxies.

### D. Unix Domain Event-Driven (`poll` vs `epoll`)

Characteristics:

- Both use non-blocking UDS connections with readiness-based I/O.
- `poll` is portable across POSIX systems; `epoll` is Linux-specific.
- Performance can differ by workload; this benchmark runs both so you can compare using the same parameters.

Why `poll` is faster than `epoll` in this specific run:

- With this test size (40 clients, 64-byte payload), the readiness set is relatively small.
- `poll` has a simple linear scan and low setup complexity for small-to-mid fd counts.
- `epoll` adds registration/management overhead (`epoll_ctl`, event queue bookkeeping), which can outweigh its scaling advantage at lower concurrency.
- This is a short-lived micro-benchmark; in larger/high-fd workloads, `epoll` often catches up or wins.

Mode notes in this project:

- `thread`: thread-per-client in AF_UNIX.
- `select` / `poll` / `epoll` / `kqueue`: event-driven AF_UNIX loops.
- Benchmark suite currently compares UDS with `poll` and `epoll`.

## 7) `select` vs `poll` vs `epoll` vs `kqueue`

### Capability and Portability Comparison

| API | Complexity (approx.) | Trigger model | Platform | Notes |
|---|---|---|---|---|
| `select` | Scan `O(n)` + `FD_SETSIZE` limit | level | Broad POSIX | Most portable, but limited fd count and lower scalability |
| `poll` | Scan `O(n)` | level | Broad POSIX | No `FD_SETSIZE`, but still linear scan |
| `epoll` | Near `O(ready)` | level / edge | Linux | Common first choice for high concurrency |
| `kqueue` | Near `O(ready)` | event filter | BSD/macOS | High-performance option in BSD ecosystems |

### Corresponding Source Code in This Project

- `select`: `run_select()` in `src/tcp_event_server.cpp`
- `poll`: `run_poll()` in `src/tcp_event_server.cpp`
- `epoll`: `run_epoll()` in `src/tcp_event_server.cpp` (`#if defined(__linux__)`)
- `kqueue`: `run_kqueue()` in `src/tcp_event_server.cpp` (BSD/macOS conditional build)

## 8) Conclusion (Based on This Run)

- Under this workload, event-driven modes are generally more memory-efficient than thread-per-client.
- In this run, `poll` / `epoll` deliver higher throughput than `select` and thread-per-client.
- Unix Domain Socket event-driven modes (`poll`, `epoll`) are strong options for same-host communication, with `poll` leading in this specific run.

> Recommendations:
> - For cross-host communication: TCP + event-driven (typically `epoll` on Linux).
> - For same-host IPC only: prioritize Unix Domain Socket.
> - For simple needs with lower connection counts: thread-per-client is fast to implement.

## 9) Reproduce the Test

```bash
make clean && make
pytest -q
```

To better match real workloads, you can tune these in `tests/test_benchmarks.py`:

- `clients`
- `loops`
- `payload_size`

