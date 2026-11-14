"""Over max-session-count: which session gives way to a new one."""
import socket
import threading
import time

from hevtest import client, load
from hevtest.case import TunnelTestCase


class SessionEvictionTest(TunnelTestCase):

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

    def udp_session(self, port):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(s.close)
        s.settimeout(1.0)
        s.connect((self.t4, port))
        return s

    def udp_ask(self, s):
        s.send(b"u")
        try:
            return s.recv(2048)
        except OSError:
            return None

    def test_max_session_count_one(self):
        # a new session must not evict itself
        t = self.tunnel(misc={"max-session-count": 1})
        for _ in range(3):
            self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100))
            self.assertIsNotNone(client.udp_echo(self.t4, self.bed.P_ECHO, b"x"))
        self.assertTrue(t.proc.alive(), t.log())

    def test_keepalive_flows_survive_storm(self):
        # flows that talk only now and then must outlive a storm of new ones
        t = self.tunnel(misc={"max-session-count": 32,
                              "evict-protect-idle": 3000})
        u = self.udp_session(self.bed.P_PEER)
        peer = self.udp_ask(u)
        self.assertIsNotNone(peer)
        tcp = self.tcp_session()
        # one exchange after half of evict-protect-idle, then the storm
        time.sleep(2.0)
        self.assertEqual(self.udp_ask(u), peer)
        self.assertTrue(self.tcp_alive(tcp))

        jobs = [threading.Thread(target=load.udp_flows,
                                 args=(self.t4, self.bed.P_SINK, 100, 6)),
                threading.Thread(target=load.tcp_storm,
                                 args=(self.t4, self.bed.P_HOLD, 300, 6,
                                       "idle"), kwargs={"rate": 50})]
        for j in jobs:
            j.start()
        lost = []
        for i in range(4):
            time.sleep(1.5)
            if self.udp_ask(u) != peer:
                lost.append("udp %d" % i)
            if not self.tcp_alive(tcp):
                lost.append("tcp %d" % i)
        for j in jobs:
            j.join()

        self.assertEqual(lost, [], t.log())
        # the storm is over, new sessions get in again
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100))
        self.assertIsNotNone(client.udp_echo(self.t4, self.bed.P_ECHO, b"x"))

    def test_protected_at_most_half(self):
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 3000})
        a, b, c, d = (self.tcp_session() for _ in range(4))
        time.sleep(2.0)
        for s in (a, b, c, d):
            self.assertTrue(self.tcp_alive(s))
        # all four are protected: the two silent the longest give way, then
        # the unprotected e, and a new session is never refused
        e = self.tcp_session()
        f = self.tcp_session()
        g = self.tcp_session()
        for s in (a, b, e):
            self.assertFalse(self.tcp_alive(s))
        for s in (c, d, f, g):
            self.assertTrue(self.tcp_alive(s))

    def test_young_answered_kept(self):
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 3000})
        a = self.tcp_session()
        for _ in range(3):
            s = client.tcp_connect(self.t4, self.bed.P_HOLD)
            self.addCleanup(s.close)
        # a is silent the longest but upstream has just answered it, one of
        # the unanswered ones gives way
        e = self.tcp_session()
        self.assertTrue(self.tcp_alive(a), "young answered one was replaced")
        self.assertTrue(self.tcp_alive(e))

    def test_young_go_before_protected(self):
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 3000})
        a = self.tcp_session()
        b = self.tcp_session()
        time.sleep(2.0)
        self.assertTrue(self.tcp_alive(a))
        self.assertTrue(self.tcp_alive(b))
        c = self.tcp_session()
        d = self.tcp_session()
        # the half is full, so a young one gives way and not a or b
        e = self.tcp_session()
        self.assertFalse(self.tcp_alive(c))
        for s in (a, b, d, e):
            self.assertTrue(self.tcp_alive(s))

    def test_protection_ends_with_silence(self):
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 1000})
        a = self.tcp_session()
        b = self.tcp_session()
        time.sleep(0.9)
        self.assertTrue(self.tcp_alive(a))
        self.assertTrue(self.tcp_alive(b))
        time.sleep(1.5)
        self.assertTrue(self.tcp_alive(b))
        c = self.tcp_session()
        d = self.tcp_session()
        # a is the oldest and has been silent for longer than
        # evict-protect-idle, b is still protected
        e = self.tcp_session()
        self.assertFalse(self.tcp_alive(a), "silent session was not replaced")
        for s in (b, c, d, e):
            self.assertTrue(self.tcp_alive(s))

    def test_young_answered_not_protected(self):
        # one-shot exchanges like DNS queries are kept for 2 s at most
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 3000})
        for s in [self.udp_session(self.bed.P_ECHO) for _ in range(2)]:
            self.assertEqual(self.udp_ask(s), b"u")
        time.sleep(2.5)
        c = self.tcp_session()
        d = self.tcp_session()
        # a one-shot one gives way, not the young c
        e = self.tcp_session()
        for s in (c, d, e):
            self.assertTrue(self.tcp_alive(s))

    def test_unanswered_not_protected(self):
        self.tunnel(misc={"max-session-count": 4,
                          "evict-protect-idle": 3000})
        dead = [self.udp_session(self.bed.P_SINK) for _ in range(2)]
        for s in dead:
            s.send(b"d")
        # talked long enough to be kept, if only they had been answered
        time.sleep(2.0)
        for s in dead:
            s.send(b"d")
        c = self.tcp_session()
        d = self.tcp_session()
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100))
        # a dead flow went first, not c
        self.assertTrue(self.tcp_alive(c))
        self.assertTrue(self.tcp_alive(d))
