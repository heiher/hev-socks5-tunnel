"""Helpers shared by the benchmarks."""
import contextlib
import resource
import socket
import threading
import time

from . import client, config


def raise_nofile(n=65536):
    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (n, n))
    except (ValueError, OSError):
        pass


@contextlib.contextmanager
def running(bed, **over):
    """Start a tunnel for the duration of the block; yields (proc, sampler)."""
    cfg = config.build(bed, **over)
    path = bed.tmpfile("bench.yml")
    with open(path, "w") as f:
        f.write(config.dump(cfg))
    proc = bed.start_tunnel(path, tun=cfg["tunnel"].get("name"))
    try:
        yield proc, Sampler(bed, proc.pid)
    finally:
        bed.stop_tunnel(proc)
        client.wait_for(lambda: bed.iface() is None, 3)


class Sampler:
    """CPU / memory of the tunnel process between ``start`` and ``stop``."""

    def __init__(self, bed, pid):
        self.bed, self.pid = bed, pid

    def start(self):
        self.t0 = time.time()
        self.s0 = self.bed.proc_stats(self.pid)

    def stop(self):
        s1 = self.bed.proc_stats(self.pid)
        wall = time.time() - self.t0
        cpu = s1.get("cpu_s", 0) - self.s0.get("cpu_s", 0)
        return {"wall_s": wall, "cpu_s": cpu,
                "cpu_pct": 100.0 * cpu / wall if wall else 0.0,
                "rss_kb": s1.get("rss_kb", 0),
                "peak_rss_kb": s1.get("peak_rss_kb", 0)}


def mbps(nbytes, secs):
    return nbytes * 8 / secs / 1e6


def run_threads(n, fn):
    """Run fn(i) in n threads, return their results."""
    out = [None] * n
    errs = []

    def w(i):
        try:
            out[i] = fn(i)
        except Exception as e:  # pragma: no cover
            errs.append(e)

    ts = [threading.Thread(target=w, args=(i,)) for i in range(n)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    if errs:
        raise errs[0]
    return out


def tcp_upload(host, port, secs, streams):
    """Returns (bytes acknowledged by the far end, elapsed seconds)."""
    chunk = b"\xa5" * (256 * 1024)
    start = time.time() + 0.2
    end = start + secs

    def one(_):
        s = client.tcp_connect(host, port, 10)
        s.settimeout(30)
        while time.time() < start:
            time.sleep(0.001)
        while time.time() < end:
            s.sendall(chunk)
        s.shutdown(socket.SHUT_WR)
        line = b""
        while not line.endswith(b"\n"):
            d = s.recv(64)
            if not d:
                break
            line += d
        s.close()
        return int(line)

    res = run_threads(streams, one)
    return sum(res), time.time() - start


def tcp_download(host, port, secs, streams):
    start = time.time() + 0.2
    end = start + secs

    def one(_):
        s = client.tcp_connect(host, port, 10)
        buf = bytearray(256 * 1024)
        n = 0
        while time.time() < start:
            time.sleep(0.001)
        t_end = None
        while True:
            r = s.recv_into(buf)
            if not r:
                break
            n += r
            if time.time() >= end:
                break
        s.close()
        return n

    res = run_threads(streams, one)
    return sum(res), secs


def percentile(sorted_vals, p):
    return sorted_vals[min(len(sorted_vals) - 1, int(len(sorted_vals) * p))]
