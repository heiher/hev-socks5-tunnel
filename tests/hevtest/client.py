"""Client-side helpers: behave like an ordinary application."""
import hashlib
import os
import socket
import threading
import time


def _family(host):
    return socket.AF_INET6 if ":" in host else socket.AF_INET


def tcp_connect(host, port, timeout=5.0):
    s = socket.socket(_family(host), socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
    except BaseException:
        s.close()
        raise
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return s


def recv_exact(s, n):
    buf = bytearray()
    while len(buf) < n:
        d = s.recv(min(1 << 20, n - len(buf)))
        if not d:
            raise ConnectionError("EOF after %d of %d bytes" % (len(buf), n))
        buf += d
    return bytes(buf)


def tcp_echo(host, port, size, timeout=10.0):
    """Send ``size`` random bytes, expect them back unchanged."""
    data = os.urandom(size)
    s = tcp_connect(host, port, timeout)
    try:
        err = []

        def writer():
            try:
                s.sendall(data)
            except OSError as e:
                err.append(e)

        t = threading.Thread(target=writer)
        t.start()
        got = recv_exact(s, size)
        t.join()
        if err:
            raise err[0]
        return got == data
    finally:
        s.close()


def tcp_upload_digest(host, port, size, timeout=20.0):
    """Upload ``size`` bytes, return True if the far end saw the same hash."""
    data = os.urandom(min(size, 1 << 20))
    h = hashlib.sha256()
    s = tcp_connect(host, port, timeout)
    try:
        sent = 0
        while sent < size:
            chunk = data[:size - sent]
            s.sendall(chunk)
            h.update(chunk)
            sent += len(chunk)
        s.shutdown(socket.SHUT_WR)
        line = b""
        while not line.endswith(b"\n"):
            d = s.recv(256)
            if not d:
                break
            line += d
        n, digest = line.decode().split()
        return int(n) == size and digest == h.hexdigest()
    finally:
        s.close()


def udp_echo(host, port, payload, timeout=3.0, retries=3):
    """One datagram round trip; returns the reply or None."""
    s = socket.socket(_family(host), socket.SOCK_DGRAM)
    s.settimeout(timeout / retries)
    try:
        for _ in range(retries):
            s.sendto(payload, (host, port))
            try:
                d, _ = s.recvfrom(65535)
                return d
            except socket.timeout:
                continue
        return None
    finally:
        s.close()


def tcp_ctrl(host, port, cmd, timeout=5.0):
    s = tcp_connect(host, port, timeout)
    try:
        s.sendall(cmd.encode() + b"\n")
        line = b""
        while not line.endswith(b"\n"):
            d = s.recv(256)
            if not d:
                break
            line += d
        return line.decode().strip()
    finally:
        s.close()


def tcp_blocked(host, port, timeout=3.0):
    """True if a TCP connection cannot be established or is torn down
    before any data can flow (used for failure-path tests)."""
    try:
        s = tcp_connect(host, port, timeout)
    except OSError:
        return True
    try:
        s.sendall(b"x")
        return s.recv(1) == b""
    except OSError:
        return True
    finally:
        s.close()


def wait_for(pred, timeout=5.0, step=0.02):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(step)
    return pred()


def dns_query(server, name, qtype=1, port=53, timeout=2.0):
    """Minimal DNS client; returns list of answer RDATA (A -> dotted quad)."""
    import struct
    qid = os.urandom(2)
    q = qid + struct.pack(">HHHHH", 0x0100, 1, 0, 0, 0)
    for label in name.split("."):
        q += bytes([len(label)]) + label.encode()
    q += b"\0" + struct.pack(">HH", qtype, 1)
    resp = udp_echo(server, port, q, timeout)
    if resp is None:
        return None
    an = struct.unpack(">H", resp[6:8])[0]
    off = 12
    while resp[off]:
        off += resp[off] + 1
    off += 5
    out = []
    for _ in range(an):
        off += 2 if resp[off] & 0xC0 == 0xC0 else 0
        _t, _c, _ttl, rl = struct.unpack(">HHIH", resp[off:off + 10])
        off += 10
        rd = resp[off:off + rl]
        out.append(".".join(map(str, rd)) if rl == 4 else rd.hex())
        off += rl
    return out
