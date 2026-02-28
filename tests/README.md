# Tests Overview

This document introduces the code structure of `test_benchmarks.py` and explains how benchmark output is produced.

## Files in `tests/`

- `test_benchmarks.py`: Core benchmark logic (build, run, measure, assert).
- `conftest.py`: Pytest hooks that collect results and print a grid summary at the end.

## `test_benchmarks.py` Structure

The file is organized into 6 logical layers:

1. **Path and constants**
   - `ROOT`, `BIN_DIR`
   - Used to locate project root and built binaries.

2. **Memory measurement helpers**
   - `read_rss_kb(pid)`
   - `RssSampler` class
   - Purpose: Sample server process RSS from `/proc/<pid>/status` and track peak memory (`peak_rss_kb`).

3. **Server readiness helpers**
   - `wait_tcp_ready(host, port)`
   - `wait_unix_ready(path)`
   - Purpose: Wait until the server is ready before starting load.

4. **Client worker functions**
   - `tcp_worker(host, port, loops, payload)`
   - `unix_worker(path, loops, payload)`
   - Purpose: Open one client connection and run request/echo loops.

5. **Benchmark orchestration**
   - `build_binaries()` (session autouse fixture)
   - `run_benchmark(...)`
   - `record_result(pytestconfig, name, result)`
   - Purpose:
     - Build binaries once per test session.
     - Start server process.
     - Run concurrent client threads.
     - Measure elapsed time, throughput, and peak RSS.
     - Store result into `pytestconfig._bench_results`.

6. **Benchmark test cases**
   - `test_thread_per_client_tcp(...)`
   - `test_event_driven_tcp_modes(...)` with `@pytest.mark.parametrize` for `select`, `poll`, `epoll`
   - `test_unix_domain_socket(...)` (runs Unix Domain server in `epoll` mode only)
   - Purpose: Benchmark each server design/mode and assert basic health thresholds.

## Execution Flow

When you run:

```bash
pytest -q
```

the runtime flow is:

1. `build_binaries()` runs first (`make clean && make`).
2. Each test calls `run_benchmark(...)` with a server command + worker type.
3. `run_benchmark(...)`:
   - launches server,
   - waits for readiness,
   - starts RSS sampling,
   - starts N client threads,
   - waits for completion,
   - computes metrics,
   - stops server.
4. Each test calls `record_result(...)` to store scenario metrics.
5. `conftest.py` (`pytest_terminal_summary`) renders all collected rows as a terminal grid.

## Reported Metrics

Each scenario records:

- `throughput_msg_per_sec`: total messages / elapsed time.
- `peak_rss_kb`: max sampled RSS of server process.
- `elapsed_sec`: wall-clock execution time for the benchmark body.
- `clients`: concurrent clients.
- `loops_per_client`: requests per client.
- `payload_size`: bytes per request.

## Tunable Parameters

Default load in current tests is:

- `clients=40`
- `loops=300`
- `payload_size=64`

You can edit these values in each test case to simulate different workloads.

## Notes

- `epoll` scenario is only valid on Linux; non-Linux platforms skip it.
- Unix Domain Socket benchmark uses `./bin/unix_domain_event_server epoll /tmp/uds_echo_bench.sock`.
- Unix Domain Socket test removes stale socket file before running.
- This test suite prints results in terminal only (no JSON output files).
