def pytest_configure(config):
    config._bench_results = {}


def _format_grid_table(rows: list[list[str]], headers: list[str]) -> str:
    table = [headers] + rows
    widths = [max(len(str(row[i])) for row in table) for i in range(len(headers))]

    def border() -> str:
        return "+" + "+".join("-" * (w + 2) for w in widths) + "+"

    def render_row(row: list[str]) -> str:
        cells = [f" {str(row[i]).ljust(widths[i])} " for i in range(len(headers))]
        return "|" + "|".join(cells) + "|"

    lines = [border(), render_row(headers), border()]
    lines.extend(render_row(row) for row in rows)
    lines.append(border())
    return "\n".join(lines)


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    results = getattr(config, "_bench_results", {})
    if not results:
        return

    ordered_names = [
        "thread_per_client_tcp",
        "event_driven_tcp_select",
        "event_driven_tcp_poll",
        "event_driven_tcp_epoll",
        "unix_domain_socket_poll",
        "unix_domain_socket_epoll",
    ]

    rows: list[list[str]] = []
    for name in ordered_names:
        if name not in results:
            continue
        result = results[name]
        rows.append(
            [
                name,
                f"{result['throughput_msg_per_sec']:.2f}",
                str(result["peak_rss_kb"]),
                f"{result['elapsed_sec']:.4f}",
                str(result["clients"]),
                str(result["loops_per_client"]),
                str(result["payload_size"]),
            ]
        )

    headers = [
        "scenario",
        "throughput(msg/s)",
        "peak_rss_kb",
        "elapsed_sec",
        "clients",
        "loops",
        "payload",
    ]
    grid = _format_grid_table(rows, headers)
    terminalreporter.write_sep("-", "benchmark summary")
    terminalreporter.write_line(grid)
