"""TCP redirection through the tunnel."""
import concurrent.futures as cf
import socket
import struct
import time

from hevtest import client
from hevtest.case import TunnelTestCase


class TcpTest(TunnelTestCase):

    def test_unreachable_without_tunnel(self):
        # negative control: the targets exist only behind the SOCKS5 server
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO, 1.0))

    def test_echo_ipv4(self):
        self.tunnel()
        for size in (1, 100, 1460, 65536, 4 << 20):
            with self.subTest(size=size):
                self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, size))

    def test_echo_ipv6(self):
        self.tunnel()
        for size in (1, 100, 1460, 65536, 4 << 20):
            with self.subTest(size=size):
                self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, size))

    def test_upload_integrity(self):
        self.tunnel()
        for host in (self.t4, self.t6):
            with self.subTest(host=host):
                self.assertTrue(client.tcp_upload_digest(
                    host, self.bed.P_DIGEST, 32 << 20))

    def test_download_stream(self):
        self.tunnel()
        for host in (self.t4, self.t6):
            with self.subTest(host=host):
                s = client.tcp_connect(host, self.bed.P_SOURCE)
                try:
                    got = len(client.recv_exact(s, 16 << 20))
                finally:
                    s.close()
                self.assertEqual(got, 16 << 20)

    def test_concurrent_connections(self):
        self.tunnel()
        with cf.ThreadPoolExecutor(64) as ex:
            futs = [ex.submit(client.tcp_echo,
                              self.t4 if i % 2 else self.t6,
                              self.bed.P_ECHO, 100000) for i in range(256)]
            self.assertTrue(all(f.result() for f in futs))

    def test_half_close(self):
        # shutdown(WR) must propagate while the reverse direction still works
        self.tunnel()
        self.assertTrue(client.tcp_upload_digest(self.t4, self.bed.P_DIGEST,
                                                 1000))

    def test_refused_target_closes_connection(self):
        self.tunnel()
        self.assertTrue(client.tcp_blocked(self.t4, 7999, 5.0))

    def test_upstream_down(self):
        # tunnel started against a port nobody listens on
        self.tunnel(socks5={"port": 1099})
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO, 5.0))

    def test_small_write_is_acked_immediately(self):
        # An application that sends a request in two small writes with
        # Nagle on (the usual "headers", then "body") waits for the ACK of
        # the first write before the second leaves.  lwIP delays that ACK
        # by up to TCP_TMR_INTERVAL (250 ms), so every such request stalls
        # until the timer fires.  Wait for the ACK in TCP_INFO, so the
        # measured time is the tunnel's alone.
        self.require(hasattr(socket, "TCP_INFO"),
                     "TCP_INFO is needed to observe the ACK timing")
        self.tunnel()
        chunk = b"GET / HTTP/1.0\r"
        limit = 20.0
        s = client.tcp_connect(self.t4, self.bed.P_SINK, nodelay=False)
        try:
            def acked():
                # tcpi_bytes_acked: the u64 at offset 120 of struct
                # tcp_info; the SYN's byte is already counted.
                info = s.getsockopt(socket.IPPROTO_TCP, socket.TCP_INFO, 256)
                return struct.unpack_from("=Q", info, 120)[0]

            def send_and_wait():
                base = acked()
                t0 = time.perf_counter()
                s.sendall(chunk)
                while acked() < base + len(chunk):
                    if time.perf_counter() - t0 > 2.0:
                        self.fail("small write was not ACKed within 2 s")
                    time.sleep(0.0005)
                return (time.perf_counter() - t0) * 1000

            # Warm-up: opening the connection wakes the lwIP timer task,
            # which would flush a pending delayed ACK early.
            send_and_wait()
            out = [send_and_wait() for _ in range(9)]
        finally:
            s.close()
        fast = [ms for ms in out if ms < limit]
        self.assertGreaterEqual(
            len(fast), 7,
            "only %d of %d small writes were ACKed within %.0f ms "
            "(lwIP's delayed-ACK timer is 250 ms): %s"
            % (len(fast), len(out), limit, ["%.1f" % ms for ms in out]))

    def test_survives_abrupt_client_close(self):
        self.tunnel()
        for _ in range(20):
            s = client.tcp_connect(self.t4, self.bed.P_ECHO)
            s.sendall(b"x" * 100000)
            s.close()
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))
