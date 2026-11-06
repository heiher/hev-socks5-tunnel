"""UDP redirection: UDP-in-UDP and UDP-in-TCP relay modes."""
import concurrent.futures as cf
import os
import socket

from hevtest import client
from hevtest.case import TunnelTestCase

# the relay buffer holds one 1500 byte frame
SIZES = (1, 64, 512, 1400, 1472)


class UdpTest(TunnelTestCase):

    def check_modes(self, mode):
        self.tunnel(socks5={"udp": mode})
        for host in (self.t4, self.t6):
            for size in SIZES:
                with self.subTest(mode=mode, host=host, size=size):
                    p = os.urandom(size)
                    self.assertEqual(
                        client.udp_echo(host, self.bed.P_ECHO, p), p)

    def test_udp_in_udp(self):
        self.check_modes("udp")

    def test_udp_in_tcp(self):
        self.check_modes("tcp")

    def test_many_sockets(self):
        self.tunnel()

        def one(i):
            p = os.urandom(200)
            return client.udp_echo(self.t4 if i % 2 else self.t6,
                                   self.bed.P_ECHO, p) == p

        with cf.ThreadPoolExecutor(32) as ex:
            self.assertTrue(all(ex.map(one, range(128))))

    def test_one_socket_many_packets(self):
        # same source port keeps its association for a series of datagrams
        self.tunnel()
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(3)
        try:
            for i in range(50):
                p = b"pkt-%d" % i
                s.sendto(p, (self.t4, self.bed.P_ECHO))
                self.assertEqual(s.recvfrom(2048)[0], p)
        finally:
            s.close()

    def test_udp_address_override(self):
        # an explicit (correct) address behaves like the advertised one
        self.tunnel(socks5={"udp-address": self.bed.socks_addr})
        p = os.urandom(300)
        self.assertEqual(client.udp_echo(self.t4, self.bed.P_ECHO, p), p)

    def test_udp_address_override_wrong(self):
        # a wrong relay address must break UDP while TCP keeps working
        self.tunnel(socks5={"udp-address": "10.200.0.77"})
        self.assertIsNone(client.udp_echo(self.t4, self.bed.P_ECHO, b"x", 1.5))
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100))

    def test_buffer_options(self):
        self.tunnel(misc={"udp-recv-buffer-size": 65536,
                          "udp-copy-buffer-nums": 4})
        p = os.urandom(1400)
        self.assertEqual(client.udp_echo(self.t4, self.bed.P_ECHO, p), p)
