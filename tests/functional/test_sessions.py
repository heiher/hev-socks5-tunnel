"""Per-type session limits and UDP queue limits."""
import socket
import time

from hevtest import client
from hevtest.case import TunnelTestCase


class SessionLimitsTest(TunnelTestCase):

    def tcp_session(self):
        s = client.tcp_connect(self.t4, self.bed.P_ECHO)
        self.addCleanup(s.close)
        s.sendall(b"a")
        self.assertEqual(client.recv_exact(s, 1), b"a")
        return s

    def tcp_alive(self, s):
        s.settimeout(3.0)
        try:
            s.sendall(b"b")
            return s.recv(1) == b"b"
        except OSError:
            return False

    def udp_session(self, port=None):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(s.close)
        s.settimeout(1.0)
        s.connect((self.t4, port or self.bed.P_ECHO))
        return s

    def udp_alive(self, s):
        s.send(b"u")
        try:
            return s.recv(2048) == b"u"
        except OSError:
            return False

    def test_max_session_count_one(self):
        # a new session must not evict itself
        t = self.tunnel(misc={"max-session-count": 1})
        for _ in range(3):
            self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100))
            self.assertIsNotNone(client.udp_echo(self.t4, self.bed.P_ECHO, b"x"))
        self.assertTrue(t.proc.alive(), t.log())

    def test_tcp_refused_while_busy(self):
        # no session has been idle long enough, the new one is reset
        self.tunnel(misc={"tcp-max-session-count": 2})
        socks = [self.tcp_session() for _ in range(2)]
        self.assertTrue(client.tcp_blocked(self.t4, self.bed.P_ECHO))
        for s in socks:
            self.assertTrue(self.tcp_alive(s))

    def test_tcp_replaces_idle(self):
        self.tunnel(misc={"tcp-max-session-count": 2,
                          "tcp-evict-min-idle": 500})
        a = self.tcp_session()
        b = self.tcp_session()
        time.sleep(1.0)
        self.assertTrue(self.tcp_alive(b))
        c = self.tcp_session()
        self.assertFalse(self.tcp_alive(a), "idle session was not replaced")
        self.assertTrue(self.tcp_alive(b))
        self.assertTrue(self.tcp_alive(c))

    def test_udp_refused_then_replaces_idle(self):
        self.tunnel(misc={"udp-max-session-count": 2,
                          "udp-evict-min-idle": 500})
        a, b, c = (self.udp_session() for _ in range(3))
        self.assertTrue(self.udp_alive(a))
        self.assertTrue(self.udp_alive(b))
        self.assertFalse(self.udp_alive(c))
        # a and b are idle by now, b is refreshed and a gives way
        self.assertTrue(self.udp_alive(b))
        self.assertTrue(self.udp_alive(c))
        self.assertTrue(self.udp_alive(b))

    def test_udp_protects_answered(self):
        self.tunnel(misc={"udp-max-session-count": 1,
                          "udp-evict-min-idle": 200,
                          "udp-evict-protect-age": 200,
                          "udp-evict-protect-idle": 5000})
        a, b = self.udp_session(), self.udp_session()
        self.assertTrue(self.udp_alive(a))
        time.sleep(0.5)
        self.assertFalse(self.udp_alive(b))
        self.assertTrue(self.udp_alive(a))

    def test_per_type_limits_are_separate(self):
        self.tunnel(misc={"udp-max-session-count": 1,
                          "tcp-max-session-count": 1})
        t = self.tcp_session()
        u = self.udp_session()
        self.assertTrue(self.udp_alive(u))
        self.assertTrue(self.tcp_alive(t))

    def test_udp_queue_limits(self):
        # datagrams sent before the relay is up are queued, the rest dropped
        burst = 200
        for misc in ({}, {"udp-queue-frames": 1},
                     {"udp-queue-total-frames": 1}):
            with self.subTest(misc=misc):
                t = self.tunnel(misc=misc)
                client.tcp_ctrl(self.t4, self.bed.P_CTRL, "udp-sink")
                s = self.udp_session(self.bed.P_SINK)
                for _ in range(burst):
                    s.send(b"q" * 64)
                time.sleep(1.0)
                _, pkts = map(int, client.tcp_ctrl(
                    self.t4, self.bed.P_CTRL, "udp-sink").split())
                if misc:
                    self.assertGreaterEqual(pkts, 1)
                    self.assertLess(pkts, burst)
                else:
                    self.assertEqual(pkts, burst)
                t.stop()
