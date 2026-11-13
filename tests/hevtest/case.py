"""Base class of the functional tests."""
import os
import unittest

from . import config


class Tunnel:
    def __init__(self, bed, proc, cfg_path):
        self.bed = bed
        self.proc = proc
        self.cfg_path = cfg_path

    def stop(self, *a, **kw):
        return self.bed.stop_tunnel(self.proc, *a, **kw)

    def log(self):
        return self.proc.log()


class TunnelTestCase(unittest.TestCase):
    """Each test gets an isolated tunnel instance via ``self.tunnel()``.

    ``bed`` is injected by the runner.  The shared server side (SOCKS5
    server without authentication and the target services) is always up.
    """
    bed = None

    def setUp(self):
        self.t4 = self.bed.target4
        self.t6 = self.bed.target6

    def write_config(self, **over):
        cfg = config.build(self.bed, **over)
        path = self.bed.tmpfile("tunnel.yml")
        with open(path, "w") as f:
            f.write(config.dump(cfg))
        return path

    def tunnel(self, wait=True, extra_args=(), raw=None, mem_limit=None,
               **over):
        """Start a tunnel.  ``over`` overrides config sections, e.g.
        ``self.tunnel(tunnel={'mtu': 1400}, socks5={'udp': 'tcp'})``."""
        tun = None
        if raw is None:
            tun = config.build(self.bed, **over)["tunnel"].get("name")
        if raw is not None:
            path = self.bed.tmpfile("tunnel.yml")
            with open(path, "w") as f:
                f.write(raw)
        else:
            path = self.write_config(**over)
        proc = self.bed.start_tunnel(path, extra_args, wait=wait, tun=tun,
                                     mem_limit=mem_limit)
        t = Tunnel(self.bed, proc, path)
        self.addCleanup(t.stop)
        return t

    def require(self, cond, why):
        if not cond:
            self.skipTest(why)

    def fail_with_log(self, tun, msg):
        self.fail("%s\n--- tunnel log ---\n%s" % (msg, tun.log()))
