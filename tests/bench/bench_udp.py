"""UDP throughput (both relay modes) and loss."""
import socket
import time

from hevtest import benchlib as bl
from hevtest import client

SIZE = 1400


def upload(bed, host, secs):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    s = socket.socket(fam, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 4 << 20)
    pkt = b"\x5a" * SIZE
    client.tcp_ctrl(host, bed.P_CTRL, "udp-sink")        # reset counters
    sent = 0
    t0 = time.time()
    end = t0 + secs
    while True:
        if sent % 128 == 0 and time.time() >= end:
            break
        try:
            s.sendto(pkt, (host, bed.P_SINK))
            sent += 1
        except OSError:
            time.sleep(0.0002)
    elapsed = time.time() - t0
    s.close()
    time.sleep(0.5)                                       # let it drain
    got_b, got_p = map(int, client.tcp_ctrl(host, bed.P_CTRL,
                                            "udp-sink").split())
    return sent, got_p, got_b, elapsed


def download(bed, host, secs):
    fam = socket.AF_INET6 if ":" in host else socket.AF_INET
    s = socket.socket(fam, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
    s.settimeout(1.0)
    s.sendto(b"SRC %d %f\n" % (SIZE, secs), (host, bed.P_SOURCE))
    buf = bytearray(2048)
    n = pkts = 0
    t0 = None
    last = None
    deadline = time.time() + secs + 3
    while time.time() < deadline:
        try:
            r = s.recv_into(buf)
        except socket.timeout:
            if pkts:
                break
            continue
        if t0 is None:
            t0 = time.time()
        last = time.time()
        n += r
        pkts += 1
    s.close()
    return pkts, n, ((last - t0) if t0 and last and last > t0 else secs)


def run(bed, rep, args):
    D = args.duration
    for mode in ("udp", "tcp"):
        title = "udp-in-%s" % mode
        with bl.running(bed, socks5={"udp": mode}) as (proc, smp):
            rep.group(title)
            for fam, host in (("ipv4", bed.target4), ("ipv6", bed.target6)):
                smp.start()
                sent, got_p, got_b, el = upload(bed, host, D)
                st = smp.stop()
                rep.add(title, "upload sent " + fam, bl.mbps(sent * SIZE, el),
                        "Mbit/s")
                rep.add(title, "upload delivered " + fam, bl.mbps(got_b, el),
                        "Mbit/s")
                rep.add(title, "upload loss " + fam,
                        100.0 * (sent - got_p) / sent if sent else 0.0, "%")
                rep.add(title, "upload tunnel cpu " + fam, st["cpu_pct"], "%")
                smp.start()
                pkts, n, el = download(bed, host, D)
                st = smp.stop()
                rep.add(title, "download received " + fam, bl.mbps(n, el),
                        "Mbit/s")
                rep.add(title, "download packets " + fam, pkts / el / 1000.0,
                        "kpps")
                rep.add(title, "download tunnel cpu " + fam, st["cpu_pct"], "%")
