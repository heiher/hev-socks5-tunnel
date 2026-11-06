"""Configuration options as seen by a user."""
import os
import signal
import subprocess
import time

from hevtest import client
from hevtest.case import TunnelTestCase


class TunnelDeviceTest(TunnelTestCase):

    def test_mtu_and_addresses(self):
        self.tunnel(tunnel={"mtu": 1400})
        info = self.bed.iface()
        self.assertEqual(info["mtu"], 1400)
        self.assertIn(self.bed.tun_ipv4, info["addrs"])
        self.assertIn(self.bed.tun_ipv6, info["addrs"])

    def test_min_mtu_transfer(self):
        self.tunnel(tunnel={"mtu": 1280})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1 << 20))

    def test_default_mtu(self):
        self.tunnel(tunnel={"mtu": None})
        self.assertEqual(self.bed.iface()["mtu"], 8500)

    def test_custom_name(self):
        self.tunnel(tunnel={"name": "hevx0"})
        self.bed.route_via("hevx0")
        self.assertIsNotNone(self.bed.iface("hevx0"))
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))

    def test_ipv4_only(self):
        self.tunnel(tunnel={"ipv6": None})
        info = self.bed.iface()
        self.assertIn(self.bed.tun_ipv4, info["addrs"])
        self.assertNotIn(self.bed.tun_ipv6, info["addrs"])
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))

    def test_ipv6_only(self):
        self.tunnel(tunnel={"ipv4": None})
        info = self.bed.iface()
        self.assertIn(self.bed.tun_ipv6, info["addrs"])
        self.assertNotIn(self.bed.tun_ipv4, info["addrs"])
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 1000))

    def test_multi_queue(self):
        self.tunnel(tunnel={"multi-queue": True})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1 << 20))
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 1 << 20))

    def test_offloads(self):
        # each offload is used only when the kernel supports it
        self.tunnel(tunnel={"offload": None}, raw=None)
        for opts in ({"tso": True}, {"uso": True}, {"jumbo": True},
                     {"tso": True, "uso": True, "jumbo": True}):
            with self.subTest(opts=opts):
                lines = "\n".join("    %s: true" % k for k in opts)
                raw = (
                    "tunnel:\n  name: '%s'\n  mtu: 8500\n  ipv4: '%s'\n"
                    "  ipv6: '%s'\n  offload:\n%s\n"
                    "socks5:\n  port: %d\n  address: '%s'\n  mark: %d\n"
                    "misc:\n  log-file: stderr\n" %
                    (self.bed.tun_name, self.bed.tun_ipv4, self.bed.tun_ipv6,
                     lines, self.bed.socks_port, self.bed.socks_addr,
                     self.bed.mark))
                t = self.tunnel(raw=raw)
                self.assertTrue(client.tcp_upload_digest(
                    self.t4, self.bed.P_DIGEST, 16 << 20), t.log())
                t.stop()


class HooksTest(TunnelTestCase):

    def script(self, name, marker):
        path = self.bed.tmpfile(name)
        with open(path, "w") as f:
            f.write("#!/bin/sh\necho \"$@\" > %s\n" % marker)
        os.chmod(path, 0o755)
        return path

    def test_post_up_and_pre_down(self):
        up_m = self.bed.tmpfile("up.marker")
        down_m = self.bed.tmpfile("down.marker")
        up = self.script("up.sh", up_m)
        down = self.script("down.sh", down_m)
        t = self.tunnel(tunnel={"post-up-script": up,
                                "pre-down-script": down})
        self.assertTrue(client.wait_for(lambda: os.path.exists(up_m), 3))
        with open(up_m) as f:
            self.assertIn(self.bed.tun_name, f.read())
        self.assertFalse(os.path.exists(down_m))
        t.stop()
        self.assertTrue(client.wait_for(lambda: os.path.exists(down_m), 3))


class LoggingTest(TunnelTestCase):

    def test_log_file_debug(self):
        path = self.bed.tmpfile("tunnel.log")
        self.tunnel(misc={"log-file": path, "log-level": "debug"})
        client.tcp_echo(self.t4, self.bed.P_ECHO, 1000)
        self.assertTrue(client.wait_for(
            lambda: os.path.exists(path) and os.path.getsize(path) > 0, 3))

    def test_quiet_at_error_level(self):
        path = self.bed.tmpfile("tunnel.log")
        self.tunnel(misc={"log-file": path, "log-level": "error"})
        client.tcp_echo(self.t4, self.bed.P_ECHO, 1000)
        time.sleep(0.2)
        size = os.path.getsize(path) if os.path.exists(path) else 0
        self.assertEqual(size, 0)


class MiscLimitsTest(TunnelTestCase):

    def test_limit_nofile(self):
        t = self.tunnel(misc={"limit-nofile": 4321})
        self.assertEqual(self.bed.nofile_limit(t.proc.pid), 4321)

    def test_small_buffers(self):
        self.tunnel(misc={"task-stack-size": 24576, "tcp-buffer-size": 4096})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 2 << 20))
        self.assertTrue(client.tcp_echo(self.t6, self.bed.P_ECHO, 2 << 20))

    def test_large_buffers(self):
        self.tunnel(misc={"tcp-buffer-size": 262144})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 4 << 20))

    def test_tcp_fastopen(self):
        self.tunnel(socks5={"tcp-fastopen": True})
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 100000))

    def test_max_session_count(self):
        # the oldest session is evicted when the cap is reached
        self.tunnel(misc={"max-session-count": 4})
        socks = []
        try:
            for _ in range(4):
                s = client.tcp_connect(self.t4, self.bed.P_ECHO)
                s.sendall(b"a")
                self.assertEqual(client.recv_exact(s, 1), b"a")
                socks.append(s)
            oldest = socks[0]
            oldest.settimeout(5.0)
            try:
                gone = oldest.recv(1) == b""
            except OSError:
                gone = True
            self.assertTrue(gone, "oldest session was not evicted")
            for s in socks[1:]:
                s.sendall(b"b")
                self.assertEqual(client.recv_exact(s, 1), b"b")
        finally:
            for s in socks:
                s.close()

    def test_tcp_idle_timeout(self):
        self.tunnel(misc={"tcp-read-write-timeout": 1000})
        s = client.tcp_connect(self.t4, self.bed.P_ECHO)
        try:
            s.sendall(b"a")
            self.assertEqual(client.recv_exact(s, 1), b"a")
            s.settimeout(8.0)
            t0 = time.time()
            try:
                closed = s.recv(1) == b""  # FIN from the tunnel ...
            except ConnectionError:
                closed = True              # ... or RST
            self.assertTrue(closed)
            self.assertLess(time.time() - t0, 6.0)
        finally:
            s.close()

    def test_tcp_active_connection_survives_timeout(self):
        self.tunnel(misc={"tcp-read-write-timeout": 1500})
        s = client.tcp_connect(self.t4, self.bed.P_ECHO)
        try:
            for _ in range(8):
                s.sendall(b"k")
                self.assertEqual(client.recv_exact(s, 1), b"k")
                time.sleep(0.4)
        finally:
            s.close()


class LifecycleTest(TunnelTestCase):

    def check_stop(self, sig):
        t = self.tunnel()
        self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))
        rc = t.stop(sig)
        self.assertEqual(rc, 0, t.log())
        self.assertTrue(client.wait_for(lambda: self.bed.iface() is None, 3),
                        "tun device left behind")

    def test_sigterm(self):
        self.check_stop(signal.SIGTERM)

    def test_sigint(self):
        self.check_stop(signal.SIGINT)

    def test_restart(self):
        for _ in range(3):
            t = self.tunnel()
            self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))
            t.stop()
            self.assertTrue(client.wait_for(lambda: self.bed.iface() is None, 3))

    def test_stop_with_active_sessions(self):
        t = self.tunnel()
        socks = [client.tcp_connect(self.t4, self.bed.P_ECHO) for _ in range(20)]
        try:
            self.assertEqual(t.stop(), 0)
        finally:
            for s in socks:
                s.close()

    def test_no_arguments(self):
        r = subprocess.run([self.bed.tunnel_bin], capture_output=True,
                           text=True, timeout=5)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("CONFIG_PATH", r.stdout + r.stderr)

    def test_missing_config(self):
        r = subprocess.run([self.bed.tunnel_bin, "/nonexistent/hev.yml"],
                           capture_output=True, text=True, timeout=5)
        self.assertNotEqual(r.returncode, 0)

    def test_invalid_config(self):
        path = self.bed.tmpfile("bad.yml")
        with open(path, "w") as f:
            f.write("tunnel: [unterminated\nsocks5: {\n")
        r = subprocess.run([self.bed.tunnel_bin, path], capture_output=True,
                           text=True, timeout=5)
        self.assertNotEqual(r.returncode, 0)

    def test_daemon_pid_file(self):
        pidf = self.bed.tmpfile("tunnel.pid")
        t = self.tunnel(misc={"pid-file": pidf}, wait=False)
        self.bed.wait_tun(t.proc, daemon=True)
        t.proc.wait(5)  # foreground process exits after forking
        self.assertTrue(client.wait_for(lambda: os.path.exists(pidf), 3))
        with open(pidf) as f:
            pid = int(f.read().strip())
        try:
            os.kill(pid, 0)
            self.assertTrue(client.tcp_echo(self.t4, self.bed.P_ECHO, 1000))
        finally:
            os.kill(pid, signal.SIGTERM)
        self.assertTrue(client.wait_for(lambda: self.bed.iface() is None, 5))
