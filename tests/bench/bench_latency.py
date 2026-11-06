"""Round-trip latency through the tunnel (small messages)."""
import socket
import time

from hevtest import benchlib as bl
from hevtest import client


def stats(vals):
    vals.sort()
    return (sum(vals) / len(vals), bl.percentile(vals, 0.5),
            bl.percentile(vals, 0.99))


def tcp_rtt(host, port, n):
    s = client.tcp_connect(host, port)
    out = []
    try:
        for i in range(n + 200):
            t = time.perf_counter()
            s.sendall(b"p")
            s.recv(1)
            if i >= 200:                               # warm-up
                out.append((time.perf_counter() - t) * 1e6)
    finally:
        s.close()
    return out


def udp_rtt(host, port, n):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    s = socket.socket(fam, socket.SOCK_DGRAM)
    s.settimeout(2)
    out = []
    try:
        for i in range(n + 100):
            t = time.perf_counter()
            s.sendto(b"p", (host, port))
            s.recvfrom(64)
            if i >= 100:
                out.append((time.perf_counter() - t) * 1e6)
    finally:
        s.close()
    return out


def run(bed, rep, args):
    n = 3000
    with bl.running(bed) as (proc, smp):
        rep.group("latency")
        for fam, host in (("ipv4", bed.target4), ("ipv6", bed.target6)):
            for proto, fn in (("tcp", tcp_rtt), ("udp", udp_rtt)):
                avg, p50, p99 = stats(fn(host, bed.P_ECHO, n))
                tag = "%s %s" % (proto, fam)
                rep.add("latency", tag + " avg", avg, "us")
                rep.add("latency", tag + " p50", p50, "us")
                rep.add("latency", tag + " p99", p99, "us")
