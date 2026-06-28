import os
import signal
import socket
import subprocess
import threading
import time
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[1]
BIN_DIR = ROOT / "bin"


def read_rss_kb(pid: int) -> int:
    status = Path(f"/proc/{pid}/status")
    if not status.exists():
        return 0
    for line in status.read_text().splitlines():
        if line.startswith("VmRSS:"):
            parts = line.split()
            return int(parts[1])
    return 0


class RssSampler:
    def __init__(self, pid: int, interval_sec: float = 0.01):
        self.pid = pid
        self.interval_sec = interval_sec
        self.peak_kb = 0
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)

    def _run(self):
        while not self._stop.is_set():
            rss = read_rss_kb(self.pid)
            self.peak_kb = max(self.peak_kb, rss)
            time.sleep(self.interval_sec)

    def start(self):
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=1)


def wait_tcp_ready(host: str, port: int, timeout: float = 5.0):
    end = time.time() + timeout
    while time.time() < end:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.2)
            try:
                s.connect((host, port))
                return
            except OSError:
                time.sleep(0.05)
    raise TimeoutError(f"tcp server not ready at {host}:{port}")


def wait_unix_ready(path: str, timeout: float = 5.0):
    end = time.time() + timeout
    while time.time() < end:
        if not Path(path).exists():
            time.sleep(0.05)
            continue
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(0.2)
            try:
                s.connect(path)
                return
            except OSError:
                time.sleep(0.05)
    raise TimeoutError(f"unix socket server not ready at {path}")


def tcp_worker(host: str, port: int, loops: int, payload: bytes):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        for _ in range(loops):
            s.sendall(payload)
            data = b""
            while len(data) < len(payload):
                chunk = s.recv(4096)
                if not chunk:
                    raise RuntimeError("tcp connection closed unexpectedly")
                data += chunk


def unix_worker(path: str, loops: int, payload: bytes):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
        s.connect(path)
        for _ in range(loops):
            s.sendall(payload)
            data = b""
            while len(data) < len(payload):
                chunk = s.recv(4096)
                if not chunk:
                    raise RuntimeError("unix connection closed unexpectedly")
                data += chunk


@pytest.fixture(scope="session", autouse=True)
def build_binaries():
    subprocess.run(["make", "clean"], cwd=ROOT, check=True)
    subprocess.run(["make"], cwd=ROOT, check=True)


def run_benchmark(server_cmd, ready_func, worker_func, worker_args, clients: int, loops: int, payload_size: int):
    payload = b"x" * payload_size
    server = subprocess.Popen(
        server_cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    try:
        ready_func()
        sampler = RssSampler(server.pid)
        sampler.start()

        start = time.perf_counter()
        threads = []
        for _ in range(clients):
            args = worker_args + (loops, payload)
            t = threading.Thread(target=worker_func, args=args)
            t.start()
            threads.append(t)

        for t in threads:
            t.join()
        elapsed = time.perf_counter() - start
        sampler.stop()

        total_msgs = clients * loops
        throughput = total_msgs / elapsed

        return {
            "elapsed_sec": elapsed,
            "throughput_msg_per_sec": throughput,
            "peak_rss_kb": sampler.peak_kb,
            "clients": clients,
            "loops_per_client": loops,
            "payload_size": payload_size,
        }
    finally:
        if server.poll() is None:
            server.send_signal(signal.SIGTERM)
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()


def record_result(pytestconfig, name: str, result: dict):
    pytestconfig._bench_results[name] = result


def test_thread_per_client_tcp(pytestconfig):
    host = "127.0.0.1"
    port = 19000
    result = run_benchmark(
        server_cmd=[str(BIN_DIR / "tcp_thread_server"), str(port)],
        ready_func=lambda: wait_tcp_ready(host, port),
        worker_func=tcp_worker,
        worker_args=(host, port),
        clients=999,
        loops=300,
        payload_size=64,
    )
    record_result(pytestconfig, "thread_per_client_tcp", result)
    assert result["throughput_msg_per_sec"] > 1000
    assert result["peak_rss_kb"] > 0


@pytest.mark.parametrize("mode,port", [("select", 19001), ("poll", 19002), ("epoll", 19003)])
def test_event_driven_tcp_modes(pytestconfig, mode: str, port: int):
    if mode == "epoll" and os.uname().sysname != "Linux":
        pytest.skip("epoll only available on Linux")

    host = "127.0.0.1"
    result = run_benchmark(
        server_cmd=[str(BIN_DIR / "tcp_event_server"), mode, str(port)],
        ready_func=lambda: wait_tcp_ready(host, port),
        worker_func=tcp_worker,
        worker_args=(host, port),
        clients=999,
        loops=300,
        payload_size=64,
    )
    record_result(pytestconfig, f"event_driven_tcp_{mode}", result)
    assert result["throughput_msg_per_sec"] > 1000
    assert result["peak_rss_kb"] > 0


@pytest.mark.parametrize(
    "mode,socket_path",
    [
        ("poll", "/tmp/uds_echo_bench_poll.sock"),
        ("epoll", "/tmp/uds_echo_bench_epoll.sock"),
    ],
)
def test_unix_domain_socket_modes(pytestconfig, mode: str, socket_path: str):
    if mode == "epoll" and os.uname().sysname != "Linux":
        pytest.skip("unix domain epoll benchmark requires Linux")

    socket_file = Path(socket_path)
    if socket_file.exists():
        socket_file.unlink()

    result = run_benchmark(
        server_cmd=[str(BIN_DIR / "unix_domain_event_server"), mode, socket_path],
        ready_func=lambda: wait_unix_ready(socket_path),
        worker_func=unix_worker,
        worker_args=(socket_path,),
        clients=999,
        loops=300,
        payload_size=64,
    )
    record_result(pytestconfig, f"unix_domain_socket_{mode}", result)
    assert result["throughput_msg_per_sec"] > 1000
    assert result["peak_rss_kb"] > 0
