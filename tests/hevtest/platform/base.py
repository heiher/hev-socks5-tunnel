"""Platform independent testbed definition.

Topology (every back-end must provide the same logical layout)::

    +-------------- client env --------------+      +------------- server env -------------+
    | test process                            |      | socks5d   (SOCKS5 upstream)          |
    |   |  default route                      |      | targets   (echo/sink/source/dns ...) |
    |   v                                     |      |                                      |
    | tun0 <- hev-socks5-tunnel --(marked)--> | link | socks_addr   (reachable directly)    |
    +-----------------------------------------+      | target4/6    (reachable ONLY via     |
                                                      |               the tunnel + socks5d) |
                                                      +--------------------------------------+
"""
import os
import signal
import subprocess
import tempfile
import time


class Proc:
    """A background process with captured output."""

    def __init__(self, argv, log_path, popen):
        self.argv = argv
        self.log_path = log_path
        self.popen = popen

    @property
    def pid(self):
        return self.popen.pid

    def alive(self):
        return self.popen.poll() is None

    def log(self):
        try:
            with open(self.log_path, "r", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def stop(self, sig=signal.SIGTERM, timeout=5.0):
        if self.popen.poll() is None:
            try:
                self.popen.send_signal(sig)
            except ProcessLookupError:
                pass
            try:
                self.popen.wait(timeout)
            except subprocess.TimeoutExpired:
                self.popen.kill()
                self.popen.wait()
        return self.popen.returncode

    def wait(self, timeout=None):
        return self.popen.wait(timeout)


class Bed:
    """Abstract testbed."""

    name = "base"

    # --- addressing (back-ends may override) ---------------------------
    socks_addr = "10.200.0.2"        # where the tunnel finds the SOCKS5 server
    socks_addr6 = "fd00:200::2"
    socks_port = 1080
    target4 = "203.0.113.10"         # only reachable through the tunnel
    target6 = "2001:db8::10"
    tun_name = "tun0"
    tun_ipv4 = "198.18.0.1"
    tun_ipv6 = "fc00::1"
    mark = 438

    # target service ports (see targets.py)
    P_ECHO = 7001
    P_SINK = 7002
    P_SOURCE = 7003
    P_DIGEST = 7004
    P_COUNT = 7005
    P_PING = 7007
    P_CTRL = 7100

    def __init__(self, tunnel_bin, tools_dir, workdir=None, verbose=False):
        self.tunnel_bin = os.path.abspath(tunnel_bin)
        self.tools_dir = tools_dir
        self.verbose = verbose
        self._tmp = tempfile.TemporaryDirectory(prefix="hevtest-", dir=workdir)
        self.workdir = self._tmp.name
        self._procs = []
        self._seq = 0

    # --- lifecycle -----------------------------------------------------
    def setup(self):
        raise NotImplementedError

    def teardown(self):
        for p in reversed(self._procs):
            p.stop()
        self._procs = []
        self._tmp.cleanup()

    def enter_client(self):
        """Move the calling thread into the client environment."""
        raise NotImplementedError

    # --- processes -----------------------------------------------------
    def tmpfile(self, name):
        self._seq += 1
        return os.path.join(self.workdir, "%d-%s" % (self._seq, name))

    def spawn_client(self, argv, name=None, **kw):
        raise NotImplementedError

    def spawn_server(self, argv, name=None, **kw):
        raise NotImplementedError

    def _track(self, proc):
        self._procs.append(proc)
        return proc

    # --- services ------------------------------------------------------
    def start_socks(self, user=None, password=None, port=None):
        argv = [os.path.join(self.tools_dir, "socks5d"), "-l", "::",
                "-p", str(port or self.socks_port)]
        if user is not None:
            argv += ["-u", user, "-w", password]
        proc = self.spawn_server(argv, "socks5d")
        self.wait_ready(proc)
        return proc

    def start_targets(self):
        import sys
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        argv = [sys.executable, "-I", os.path.join(here, "targets.py")]
        proc = self.spawn_server(argv, "targets")
        self.wait_ready(proc)
        return proc

    def wait_ready(self, proc, timeout=10.0):
        end = time.time() + timeout
        while time.time() < end:
            if "READY" in proc.log():
                return
            if not proc.alive():
                raise RuntimeError("%s exited early:\n%s" %
                                   (proc.argv[0], proc.log()))
            time.sleep(0.01)
        raise RuntimeError("%s did not become ready" % proc.argv[0])

    # --- tunnel --------------------------------------------------------
    def start_tunnel(self, config_path, extra_args=(), wait=True):
        """Start the tunnel in the client env and wire up routing."""
        raise NotImplementedError

    def stop_tunnel(self, proc, sig=signal.SIGTERM):
        return proc.stop(sig)

    # --- introspection -------------------------------------------------
    def iface(self, name=None):
        """Return dict(mtu=, addrs=[...], up=) for an interface or None."""
        raise NotImplementedError

    def proc_stats(self, pid):
        """Return dict(rss_kb=, peak_rss_kb=, cpu_s=) for a process."""
        raise NotImplementedError

    def ping(self, addr, count=1, timeout=2):
        raise NotImplementedError

    def nofile_limit(self, pid):
        raise NotImplementedError
