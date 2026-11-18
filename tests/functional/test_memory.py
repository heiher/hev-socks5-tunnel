"""Memory limits: the tunnel runs with a hard memory limit (the OS kills it
beyond, like iOS does with a Network Extension) and must survive a sudden
surge of connections, at the cost of the sessions it has to drop."""
import concurrent.futures as cf
import socket
import threading
import time

from hevtest import benchlib, client, load
from hevtest.case import TunnelTestCase

MiB = 1 << 20

# What a 50 MiB budget (iOS NE) is configured with, see README.
LIMIT = 50 * MiB
LIMITED = {
    "max-session-count": 200,
    "max-buffer-size": 4 * MiB,
    "tcp-buffer-size": 16384,
    "task-stack-size": 36864,
    "tcp-socket-buffer-size": 32768,
    "udp-recv-buffer-size": 65536,
}


class MemoryLimitTest(TunnelTestCase):

    def setUp(self):
        super().setUp()
        benchlib.raise_nofile()

    def limited(self, **misc):
        self.require(self.bed.memcg_supported(),
                     "memory limits are not available (cgroup v2 memory)")
        m = dict(LIMITED)
        m.update(misc)
        return self.tunnel(mem_limit=LIMIT, misc=m)

    def assert_survived(self, tun, what):
        st = self.bed.memcg_stats(tun.proc)
        if not tun.proc.alive() or st.get("events_oom_kill"):
            self.fail_with_log(tun, "%s: tunnel killed (rc %s, %s)" % (
                what, tun.proc.popen.returncode, st))
        # still serving new sessions once the surge is over
        ok = False
        for _ in range(10):
            try:
                ok = client.tcp_echo(self.t4, self.bed.P_ECHO, 100000, 10)
                break
            except OSError:
                time.sleep(0.5)
        if not ok:
            self.fail_with_log(tun, "%s: tunnel does not serve any more" % what)
        return st

    def test_surge_tcp_upload_to_stalled_server(self):
        # each connection fills its window and the upstream socket
        tun = self.limited()
        load.tcp_storm(self.t4, self.bed.P_HOLD, 2000, 6, "push")
        self.assert_survived(tun, "upload")

    def test_surge_tcp_download_to_stalled_client(self):
        # the server sends as fast as it can, nobody reads
        tun = self.limited()
        load.tcp_storm(self.t4, self.bed.P_SOURCE, 1000, 6, "stall")
        self.assert_survived(tun, "download")

    def test_surge_tcp_interactive(self):
        tun = self.limited()
        st = load.tcp_storm(self.t4, self.bed.P_ECHO, 2000, 6, "echo")
        self.assert_survived(tun, "echo")
        self.assertGreater(st["echoes"], 0)

    def test_surge_udp_flows(self):
        tun = self.limited()
        load.udp_flood(self.t4, self.bed.P_SINK, 1000, 6, 8000)
        self.assert_survived(tun, "udp")

    def test_surge_mixed(self):
        tun = self.limited()
        jobs = [
            lambda: load.tcp_storm(self.t4, self.bed.P_HOLD, 1000, 6, "push"),
            lambda: load.tcp_storm(self.t6, self.bed.P_HOLD, 1000, 6, "push"),
            lambda: load.tcp_storm(self.t4, self.bed.P_ECHO, 1000, 6, "idle"),
            lambda: load.udp_flood(self.t6, self.bed.P_SINK, 500, 6, 1400),
        ]
        ts = [threading.Thread(target=j) for j in jobs]
        for t in ts:
            t.start()
        for t in ts:
            t.join()
        self.assert_survived(tun, "mixed")


class BufferBudgetTest(TunnelTestCase):
    """A small buffer budget shrinks the receive windows; data must still
    arrive intact and nothing may get stuck."""

    def setUp(self):
        super().setUp()
        benchlib.raise_nofile()

    def test_integrity_with_small_budget(self):
        self.tunnel(misc={"max-buffer-size": 256 * 1024,
                          "tcp-socket-buffer-size": 16384})
        self.assertTrue(client.tcp_upload_digest(self.t4, self.bed.P_DIGEST,
                                                 32 << 20))
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 4 << 20))
        s = client.tcp_connect(self.t4, self.bed.P_SOURCE)
        try:
            self.assertEqual(len(client.recv_exact(s, 16 << 20)), 16 << 20)
        finally:
            s.close()

    def test_concurrent_with_overcommitted_budget(self):
        # the window floors of these sessions exceed the budget
        self.tunnel(misc={"max-buffer-size": 512 * 1024})
        with cf.ThreadPoolExecutor(32) as ex:
            futs = [ex.submit(client.tcp_upload_digest,
                              self.t4 if i % 2 else self.t6,
                              self.bed.P_DIGEST, 4 << 20, 60)
                    for i in range(32)]
            res = [f.result() for f in futs]
        # evicted ones fail, the others must be intact
        self.assertTrue(any(res))
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100000))

    def test_udp_with_small_budget(self):
        self.tunnel(misc={"max-buffer-size": 64 * 1024})
        self.assertTrue(client.udp_echo(self.t4, self.bed.P_ECHO, b"x" * 1000))


class EvictionTest(TunnelTestCase):
    """Over max-session-count, sessions that do not move any data go first,
    even if they keep being woken up by data they cannot forward."""

    def test_stalled_sessions_go_first(self):
        self.tunnel(misc={"max-session-count": 40})
        active = []
        for i in range(10):
            s = client.tcp_connect(self.t4, self.bed.P_ECHO)
            active.append(s)
        stop = threading.Event()
        errors = []

        def chat():
            buf = b"x" * 512
            while not stop.is_set():
                for s in active:
                    try:
                        s.sendall(buf)
                        client.recv_exact(s, len(buf))
                    except OSError as e:
                        errors.append(e)
                        return
                time.sleep(0.01)

        t = threading.Thread(target=chat)
        t.start()
        try:
            # far more stalled uploads than the cap
            load.tcp_storm(self.t4, self.bed.P_HOLD, 200, 4, "push", rate=100)
        finally:
            stop.set()
            t.join()
            for s in active:
                s.close()
        self.assertEqual(errors, [])
