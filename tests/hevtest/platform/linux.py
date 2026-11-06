"""Linux back-end: two network namespaces joined by a veth pair."""
import json
import os
import re
import subprocess
import time

from . import base


def sh(*argv, check=True):
    r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True)
    if check and r.returncode != 0:
        raise RuntimeError("%s failed (%d): %s" %
                           (" ".join(argv), r.returncode, r.stdout.strip()))
    return r


class Bed(base.Bed):
    name = "linux"

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        tag = "hevt%d" % os.getpid()
        self.ns_c = tag + "c"
        self.ns_s = tag + "s"
        self._created = []

    # --- lifecycle -----------------------------------------------------
    def setup(self):
        if os.geteuid() != 0:
            raise SystemExit("hevtest: root privileges are required "
                             "(tun device + network namespaces)")
        if not os.path.exists("/dev/net/tun"):
            raise SystemExit("hevtest: /dev/net/tun is not available")
        for ns in (self.ns_c, self.ns_s):
            sh("ip", "netns", "add", ns)
            self._created.append(ns)
            sh("ip", "-n", ns, "link", "set", "lo", "up")

        sh("ip", "link", "add", "hvc0", "netns", self.ns_c, "type", "veth",
           "peer", "name", "hvs0", "netns", self.ns_s)
        c = ["ip", "-n", self.ns_c]
        s = ["ip", "-n", self.ns_s]
        sh(*c, "addr", "add", "10.200.0.1/24", "dev", "hvc0")
        sh(*c, "addr", "add", "fd00:200::1/64", "dev", "hvc0", "nodad")
        sh(*s, "addr", "add", "10.200.0.2/24", "dev", "hvs0")
        sh(*s, "addr", "add", "fd00:200::2/64", "dev", "hvs0", "nodad")
        sh(*c, "link", "set", "hvc0", "up")
        sh(*s, "link", "set", "hvs0", "up")

        # remote "internet": exists only inside the server environment
        sh(*s, "addr", "add", self.target4 + "/32", "dev", "lo")
        sh(*s, "addr", "add", self.target6 + "/128", "dev", "lo", "nodad")

        # policy routing as described in the README
        for fam in ("-4", "-6"):
            sh(*c, fam, "rule", "add", "fwmark", str(self.mark), "lookup",
               "main", "pref", "10")
            sh(*c, fam, "rule", "add", "lookup", "20", "pref", "20")

    def teardown(self):
        super().teardown()
        for ns in self._created:
            sh("ip", "netns", "del", ns, check=False)
        self._created = []

    def enter_client(self):
        fd = os.open("/run/netns/" + self.ns_c, os.O_RDONLY)
        try:
            os.setns(fd, os.CLONE_NEWNET)
        finally:
            os.close(fd)

    # --- processes -----------------------------------------------------
    def _spawn(self, argv, name, **kw):
        import subprocess as sp
        log = self.tmpfile((name or os.path.basename(argv[0])) + ".log")
        f = open(log, "wb")
        popen = sp.Popen(argv, stdin=sp.DEVNULL, stdout=f, stderr=sp.STDOUT,
                         **kw)
        f.close()
        return self._track(base.Proc(argv, log, popen))

    def spawn_client(self, argv, name=None, **kw):
        # the calling thread already lives in the client namespace
        return self._spawn(argv, name, **kw)

    def spawn_server(self, argv, name=None, **kw):
        return self._spawn(["ip", "netns", "exec", self.ns_s] + list(argv),
                           name, **kw)

    # --- tunnel --------------------------------------------------------
    def start_tunnel(self, config_path, extra_args=(), wait=True, tun=None):
        tun = tun or self.tun_name
        proc = self.spawn_client([self.tunnel_bin, config_path] +
                                 list(extra_args), "tunnel")
        if wait:
            self.wait_tun(proc, tun)
        return proc

    def wait_tun(self, proc, tun=None, timeout=10.0, daemon=False):
        tun = tun or self.tun_name
        end = time.time() + timeout
        while time.time() < end:
            info = self.iface(tun)
            if info and info["up"] and info["addrs"]:
                break
            if not daemon and not proc.alive():
                raise RuntimeError("tunnel exited early (%s):\n%s" %
                                   (proc.popen.returncode, proc.log()))
            time.sleep(0.02)
        else:
            raise RuntimeError("tunnel interface did not come up:\n" +
                               proc.log())
        self.route_via(tun)

    def route_via(self, tun):
        sh("ip", "-4", "route", "replace", "default", "dev", tun, "table", "20")
        sh("ip", "-6", "route", "replace", "default", "dev", tun, "table", "20")
        for key in ("all", tun):
            try:
                with open("/proc/sys/net/ipv4/conf/%s/rp_filter" % key, "w") as f:
                    f.write("0")
            except OSError:
                pass

    # --- introspection -------------------------------------------------
    def iface(self, name=None):
        name = name or self.tun_name
        r = sh("ip", "-j", "addr", "show", "dev", name, check=False)
        if r.returncode != 0:
            return None
        try:
            d = json.loads(r.stdout)[0]
        except (ValueError, IndexError):
            return None
        return {
            "mtu": d.get("mtu"),
            "up": "UP" in d.get("flags", []),
            "addrs": [a["local"] for a in d.get("addr_info", [])],
        }

    def proc_stats(self, pid):
        out = {}
        try:
            with open("/proc/%d/status" % pid) as f:
                for line in f:
                    m = re.match(r"(VmRSS|VmHWM):\s+(\d+) kB", line)
                    if m:
                        out["rss_kb" if m.group(1) == "VmRSS" else
                            "peak_rss_kb"] = int(m.group(2))
            with open("/proc/%d/stat" % pid) as f:
                rest = f.read().rsplit(")", 1)[1].split()
            tck = os.sysconf("SC_CLK_TCK")
            out["cpu_s"] = (int(rest[11]) + int(rest[12])) / tck
        except (OSError, IndexError):
            pass
        return out

    def ping(self, addr, count=1, timeout=2):
        fam = "-6" if ":" in addr else "-4"
        r = sh("ping", fam, "-n", "-c", str(count), "-W", str(timeout), addr,
               check=False)
        return r.returncode == 0

    def nofile_limit(self, pid):
        with open("/proc/%d/limits" % pid) as f:
            for line in f:
                if line.startswith("Max open files"):
                    return int(line.split()[3])
        return None
