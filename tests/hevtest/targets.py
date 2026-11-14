"""Target services that live on the far side of the tunnel.

Runs inside the server environment (stdlib only).  Everything listens on
``::`` (dual stack) so the same service answers IPv4 and IPv6.

  TCP 7001 echo           UDP 7001 echo
  TCP 7002 sink           UDP 7002 sink  (counted, see ctrl)
  TCP 7003 source         UDP 7003 source  ("SRC <size> <seconds>\\n")
  TCP 7004 digest  (read to EOF, answer "<bytes> <sha256>\\n")
  TCP 7005 count   (read to EOF, answer "<bytes>\\n")
  TCP 7006 hold    (accept, never read nor write, close on peer hangup)
  TCP 7007 ping    (echo, one thread for all connections)
                          UDP 7008 peer    (answers "<address> <port>" it saw)
  TCP 7009 slow    (echo, the first answer after 0.3-0.8 s like a handshake,
                    or after <ms> if the first request is "d<ms>"; for
                    "t<a>,<b>" "a" after <a> ms, then "b" after <b> more)
  TCP 7100 ctrl    ("udp-sink\\n" -> "<bytes> <packets>\\n", then resets)
"""
import hashlib
import random
import socket
import threading
import time

BUF = 256 * 1024
stats = {"bytes": 0, "pkts": 0}
stats_lock = threading.Lock()


def tcp_hold(port):
    """Accepted connections are kept, without a thread, and never read, so
    whatever the peer sends piles up in front of them.  They are closed when
    the peer hangs up."""
    import select
    s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    s.bind(("::", port))
    s.listen(4096)
    hup = select.POLLRDHUP | select.POLLHUP | select.POLLERR

    def loop():
        p = select.poll()
        p.register(s, select.POLLIN)
        held = {}
        while True:
            for fd, ev in p.poll():
                if fd == s.fileno():
                    try:
                        c, _ = s.accept()
                    except OSError:
                        continue
                    held[c.fileno()] = c
                    p.register(c, hup)
                elif ev & hup:
                    p.unregister(fd)
                    held.pop(fd).close()

    threading.Thread(target=loop, daemon=True).start()


def tcp_ping(port):
    """Echo served by one thread for all connections, for many short ones:
    a thread per connection costs more than the tunnel then, and where the
    kernel places each new thread (e.g. on another L3 cache) is measured
    instead.  What can't be sent right away waits until the peer reads."""
    import selectors
    s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    s.bind(("::", port))
    s.listen(4096)
    s.setblocking(False)
    sel = selectors.DefaultSelector()
    sel.register(s, selectors.EVENT_READ)
    out = {}

    def close(c):
        sel.unregister(c)
        del out[c]
        c.close()

    def loop():
        while True:
            for key, _ in sel.select():
                c = key.fileobj
                if c is s:
                    try:
                        c, _ = s.accept()
                    except OSError:
                        continue
                    c.setblocking(False)
                    c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    out[c] = b""
                    sel.register(c, selectors.EVENT_READ)
                    continue
                try:
                    if not out[c]:
                        out[c] = c.recv(BUF)
                        if not out[c]:
                            close(c)
                            continue
                    out[c] = out[c][c.send(out[c]):]
                except BlockingIOError:
                    pass
                except OSError:
                    close(c)
                    continue
                ev = selectors.EVENT_WRITE if out[c] else selectors.EVENT_READ
                if ev != key.events:
                    sel.modify(c, ev)

    threading.Thread(target=loop, daemon=True).start()


def tcp_listener(port, handler):
    s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    s.bind(("::", port))
    s.listen(4096)

    def loop():
        while True:
            try:
                c, _ = s.accept()
            except OSError:
                return
            c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            threading.Thread(target=run, args=(c,), daemon=True).start()

    def run(c):
        try:
            handler(c)
        except OSError:
            pass
        finally:
            c.close()

    threading.Thread(target=loop, daemon=True).start()


def udp_listener(port, handler):
    s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 * 1024 * 1024)
    s.bind(("::", port))
    threading.Thread(target=handler, args=(s,), daemon=True).start()


def h_echo(c):
    while True:
        d = c.recv(BUF)
        if not d:
            return
        c.sendall(d)


def h_slow(c):
    d = c.recv(BUF)
    if not d:
        return
    if d.startswith(b"t"):
        first, rest = (int(x) / 1000.0 for x in d[1:].split(b","))
        time.sleep(first)
        c.sendall(b"a")
        time.sleep(rest)
        c.sendall(b"b")
        return
    if d.startswith(b"d") and d[1:].isdigit():
        time.sleep(int(d[1:]) / 1000.0)
    else:
        time.sleep(random.uniform(0.3, 0.8))
    c.sendall(d)
    h_echo(c)


def h_sink(c):
    buf = bytearray(BUF)
    while c.recv_into(buf):
        pass


def h_source(c):
    d = b"\x5a" * BUF
    while True:
        c.sendall(d)


def h_digest(c):
    h = hashlib.sha256()
    n = 0
    buf = bytearray(BUF)
    mv = memoryview(buf)
    while True:
        r = c.recv_into(buf)
        if not r:
            break
        n += r
        h.update(mv[:r])
    c.sendall(("%d %s\n" % (n, h.hexdigest())).encode())


def h_count(c):
    n = 0
    buf = bytearray(BUF)
    while True:
        r = c.recv_into(buf)
        if not r:
            break
        n += r
    c.sendall(("%d\n" % n).encode())


def h_ctrl(c):
    f = c.makefile("rwb")
    for line in f:
        cmd = line.strip().decode()
        if cmd == "udp-sink":
            with stats_lock:
                out = "%d %d\n" % (stats["bytes"], stats["pkts"])
                stats["bytes"] = stats["pkts"] = 0
            f.write(out.encode())
            f.flush()
        else:
            f.write(b"ERR\n")
            f.flush()


def u_echo(s):
    while True:
        d, a = s.recvfrom(65535)
        s.sendto(d, a)


def u_sink(s):
    while True:
        d, _ = s.recvfrom(65535)
        with stats_lock:
            stats["bytes"] += len(d)
            stats["pkts"] += 1


def u_peer(s):
    while True:
        _, a = s.recvfrom(65535)
        s.sendto(("%s %d" % a[:2]).encode(), a)


def u_source(s):
    while True:
        d, a = s.recvfrom(65535)
        try:
            _, size, secs = d.split()
            size, secs = int(size), float(secs)
        except ValueError:
            continue
        threading.Thread(target=blast, args=(s, a, size, secs),
                         daemon=True).start()


def blast(s, addr, size, secs):
    pkt = b"\xa5" * size
    end = time.time() + secs
    n = 0
    while True:
        if n % 128 == 0 and time.time() >= end:
            break
        try:
            s.sendto(pkt, addr)
        except OSError:
            time.sleep(0.0005)
        n += 1


def main():
    tcp_listener(7001, h_echo)
    tcp_listener(7002, h_sink)
    tcp_listener(7003, h_source)
    tcp_listener(7004, h_digest)
    tcp_listener(7005, h_count)
    tcp_hold(7006)
    tcp_ping(7007)
    tcp_listener(7009, h_slow)
    tcp_listener(7100, h_ctrl)
    udp_listener(7001, u_echo)
    udp_listener(7002, u_sink)
    udp_listener(7003, u_source)
    udp_listener(7008, u_peer)
    print("READY", flush=True)
    threading.Event().wait()


if __name__ == "__main__":
    main()
