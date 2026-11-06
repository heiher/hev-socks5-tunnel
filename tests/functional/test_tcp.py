"""TCP redirection through the tunnel."""
import concurrent.futures as cf

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

    def test_survives_abrupt_client_close(self):
        self.tunnel()
        for _ in range(20):
            s = client.tcp_connect(self.t4, self.bed.P_ECHO)
            s.sendall(b"x" * 100000)
            s.close()
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))
