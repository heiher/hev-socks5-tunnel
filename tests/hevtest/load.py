"""Load generators: many sockets driven from one thread (epoll), so that a
burst of thousands of connections does not need thousands of threads.

Every generator runs for ``secs`` and returns a dict of counters; nothing
here asserts, the caller decides what is acceptable.
"""
import errno
import select
import socket
import time

CHUNK = b"\x5a" * (64 * 1024)


def _family(host):
    return socket.AF_INET6 if ":" in host else socket.AF_INET


class _Conn:
    __slots__ = ("sock", "state", "t0", "rx", "tx", "want")

    def __init__(self, sock):
        self.sock = sock
        self.state = "connecting"
        self.t0 = time.time()
        self.rx = self.tx = 0
        self.want = 0


def tcp_storm(host, port, count, secs, mode, rate=0, payload=CHUNK,
              connect_timeout=10.0):
    """Open ``count`` TCP connections (all at once, or ``rate`` per second)
    and keep them busy until ``secs`` have passed.

    mode: ``push``  write as fast as the connection accepts,
          ``stall`` connect and never read (pair with a source),
          ``echo``  send 1 KiB, wait for it to come back, repeat,
          ``idle``  connect and keep the connection open.
    """
    ep = select.epoll()
    conns = {}
    st = {"opened": 0, "established": 0, "failed": 0, "reset": 0,
          "rx": 0, "tx": 0, "echoes": 0, "timeouts": 0}
    t_start = time.time()
    end = t_start + secs
    buf = bytearray(64 * 1024)
    ping = payload[:1024]

    def close(c, key=None):
        try:
            ep.unregister(c.sock)
        except (OSError, ValueError):
            pass
        conns.pop(c.sock.fileno(), None)
        c.sock.close()
        if key:
            st[key] += 1

    def open_one():
        s = socket.socket(_family(host), socket.SOCK_STREAM)
        s.setblocking(False)
        try:
            s.connect((host, port))
        except BlockingIOError:
            pass
        except OSError:
            s.close()
            st["failed"] += 1
            return
        c = _Conn(s)
        conns[s.fileno()] = c
        ep.register(s, select.EPOLLOUT)
        st["opened"] += 1

    def on_established(c):
        c.state = "up"
        st["established"] += 1
        if mode == "push":
            ep.modify(c.sock, select.EPOLLOUT | select.EPOLLIN)
        elif mode == "echo":
            send_ping(c)
        else:
            # stall/idle: only watch for the far end going away
            ep.modify(c.sock, select.EPOLLRDHUP)

    def send_ping(c):
        try:
            c.sock.send(ping)
            c.want = len(ping)
            c.tx += len(ping)
        except OSError:
            close(c, "reset")
            return
        ep.modify(c.sock, select.EPOLLIN)

    opened_target = count
    while True:
        now = time.time()
        if now >= end:
            break
        want = count if not rate else min(count, int((now - t_start) * rate))
        while st["opened"] + st["failed"] < min(want, opened_target):
            open_one()
        for c in list(conns.values()):
            if c.state == "connecting" and now - c.t0 > connect_timeout:
                close(c, "timeouts")
        for fd, ev in ep.poll(0.05):
            c = conns.get(fd)
            if not c:
                continue
            if c.state == "connecting":
                e = c.sock.getsockopt(socket.SOL_SOCKET, socket.SO_ERROR)
                if e:
                    close(c, "failed")
                    continue
                on_established(c)
                continue
            if ev & (select.EPOLLERR | select.EPOLLHUP | select.EPOLLRDHUP):
                if mode in ("stall", "idle"):
                    close(c, "reset")
                    continue
            if ev & select.EPOLLIN:
                try:
                    n = c.sock.recv_into(buf)
                except BlockingIOError:
                    n = -1
                except OSError:
                    close(c, "reset")
                    continue
                if n == 0:
                    close(c, "reset")
                    continue
                if n > 0:
                    c.rx += n
                    st["rx"] += n
                    if mode == "echo":
                        c.want -= n
                        if c.want <= 0:
                            st["echoes"] += 1
                            send_ping(c)
                            continue
            if mode == "push" and ev & select.EPOLLOUT:
                try:
                    while True:
                        n = c.sock.send(payload)
                        c.tx += n
                        st["tx"] += n
                except BlockingIOError:
                    pass
                except OSError:
                    close(c, "reset")
                    continue
    st["alive"] = sum(1 for c in conns.values() if c.state == "up")
    for c in list(conns.values()):
        close(c)
    ep.close()
    return st


def udp_flows(host, port, rate, secs):
    """Open ``rate`` new UDP flows per second, one datagram each, like
    retries to a dead server or DNS queries; counts the ones answered."""
    socks = []
    st = {"opened": 0, "answered": 0}
    end = time.time() + secs
    while time.time() < end:
        s = socket.socket(_family(host), socket.SOCK_DGRAM)
        s.setblocking(False)
        socks.append(s)
        try:
            s.sendto(b"s", (host, port))
            st["opened"] += 1
        except OSError:
            pass
        time.sleep(1.0 / rate)
    for s in socks:
        try:
            s.recv(16)
            st["answered"] += 1
        except OSError:
            pass
        s.close()
    return st


def udp_flood(host, port, flows, secs, size=1400, pps=0):
    """Send datagrams of ``size`` from ``flows`` sockets (one tunnel session
    each) for ``secs``, as fast as possible or ``pps`` in total."""
    socks = []
    st = {"flows": 0, "sent": 0, "dropped": 0, "rx": 0}
    for _ in range(flows):
        s = socket.socket(_family(host), socket.SOCK_DGRAM)
        s.setblocking(False)
        try:
            s.connect((host, port))
        except OSError:
            s.close()
            continue
        socks.append(s)
    st["flows"] = len(socks)
    pkt = b"\xa5" * size
    buf = bytearray(65536)
    t0 = time.time()
    end = t0 + secs
    i = 0
    while socks and time.time() < end:
        if pps:
            budget = int((time.time() - t0) * pps) - st["sent"]
            if budget <= 0:
                time.sleep(0.001)
                continue
        else:
            budget = len(socks)
        for _ in range(min(budget, 4096)):
            s = socks[i % len(socks)]
            i += 1
            try:
                s.send(pkt)
                st["sent"] += 1
            except OSError as e:
                if e.errno not in (errno.EAGAIN, errno.ENOBUFS,
                                   errno.ECONNREFUSED):
                    raise
                st["dropped"] += 1
            try:
                st["rx"] += s.recv_into(buf)
            except OSError:
                pass
    for s in socks:
        s.close()
    return st
