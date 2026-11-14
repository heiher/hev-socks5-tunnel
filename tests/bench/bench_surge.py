"""Surges of new connections: what the live ones go through meanwhile.

The tunnel runs under a memory limit (50 MiB, as an iOS Network Extension,
where the OS kills it beyond) with long-lived flows that a user would notice
breaking, while new connections are opened at a high rate:

  call       UDP echo, 50 datagrams/s; reported: datagrams lost
  keepalive  UDP and TCP, one exchange every 4 s; reported: exchanges that
             went through, and for UDP the ones answered from another
             upstream address (the flow was moved to a new session, which
             breaks it for the far end)
  idle       TCP opened before, silent until after the surge
  download   TCP, as fast as it goes; reported: Mbit/s during the surge
  probe      a new TCP and UDP exchange each second during the surge
  handshake  10 new TCP connections a second during the surge whose server
             answers the first request after 0.3-0.8 s (``handshake_ms``
             sets it; with ``first_ms`` it also answers once that early,
             like a TLS server hello); reported: the ones answered, and of
             the rest the ones reset at once (refused), reset later (evicted
             before the answer) and timed out
  page       every 5 s, 50 such connections at once (``page``), like a web
             page; reported: pages that loaded completely and how long it
             took, during the surge and after it, and the same outcomes

Surges, 150 new flows/s unless noted:

  udp-dead   one datagram each, never answered (retries to a dead server)
  udp-300    the same at 300/s
  dns        one datagram each, answered once
  quic       both ways every 100 ms for as long as the surge lasts (50/s)
  tcp-dead   connected, nothing ever comes back
  tcp-300    the same at 300/s
  tcp-fill   writing as fast as the window allows to a peer that never reads
  mixed      udp-dead and tcp-dead at the same time
  bursts     a second of tcp-dead at 300/s every 5 s
  no-answer  tcp-dead to an address upstream can't reach, so its connect
             hangs until it gives up

HEVTEST_SURGE takes JSON that overrides SETUP below: the configs compared
(name -> misc section, e.g. to run other builds with their own keys), the
surges, the memory limit, the timing, extra UDP keep-alives
(``"keepalives": [[count, interval_s], ...]``, reported per interval),
``"netem"`` (e.g. ``"delay 150ms loss 1%"``, added both ways between the
tunnel and the server), ``"cpu"`` (the tunnel is pinned to that CPU next to a
busy loop) and ``"out"`` (a file each result is appended to as JSON).

It takes about fifteen minutes, so it runs only when asked for with
``-k surge``.
"""
import collections
import json
import os
import random
import socket
import statistics
import subprocess
import sys
import threading
import time

from hevtest import client, config, load
from hevtest import benchlib as bl

ON_REQUEST = True

MiB = config.MiB
NO_ANSWER = "10.200.0.99"

SETUP = {
    "configs": {
        "limits": config.LOW_MEMORY,
        "limits+protect": dict(config.LOW_MEMORY,
                               **{"evict-protect-idle": 10000}),
    },
    "surges": ["udp-dead", "udp-300", "dns", "quic", "tcp-dead", "tcp-300",
               "tcp-fill", "mixed", "bursts", "no-answer"],
    "mem_mib": 50,
    "rate": 150,
    "warm": 10,
    "secs": 15,
    "after": 10,
    "keepalives": [],
    "handshake_ms": None,
    "first_ms": None,
    "page": 50,
    "netem": None,
    "cpu": None,
    "out": None,
}


def setup():
    st = dict(SETUP)
    st.update(json.loads(os.environ.get("HEVTEST_SURGE") or "{}"))
    return st


def quic_like(bed, rate, secs):
    """New UDP flows at ``rate`` that keep talking both ways."""
    socks, n = [], 0
    t0 = time.monotonic()
    tick = 0.0
    while time.monotonic() - t0 < secs:
        now = time.monotonic() - t0
        while n < now * rate:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.setblocking(False)
            s.connect((bed.target4, bed.P_ECHO))
            socks.append(s)
            n += 1
        if now - tick >= 0.1:
            tick = now
            for s in socks:
                try:
                    s.send(b"q" * 200)
                except OSError:
                    pass
                try:
                    s.recv(2048)
                except OSError:
                    pass
        time.sleep(0.005)
    for s in socks:
        s.close()
    return {"new": n}


def bursts(bed, rate, secs):
    out = {}
    end = time.monotonic() + secs
    while time.monotonic() < end:
        st = load.tcp_storm(bed.target4, bed.P_HOLD, rate, 1.0, "idle",
                            rate=rate)
        for k, v in st.items():
            out[k] = out.get(k, 0) + v
        time.sleep(max(0.0, min(4.0, end - time.monotonic())))
    return out


def surge(bed, kind, st):
    t4, rate, secs = bed.target4, st["rate"], st["secs"]
    if kind == "udp-dead":
        return load.udp_flows(t4, bed.P_SINK, rate, secs)
    if kind == "udp-300":
        return load.udp_flows(t4, bed.P_SINK, 2 * rate, secs)
    if kind == "dns":
        return load.udp_flows(t4, bed.P_ECHO, rate, secs)
    if kind == "quic":
        return quic_like(bed, rate // 3, secs)
    if kind == "tcp-dead":
        return load.tcp_storm(t4, bed.P_HOLD, rate * secs, secs, "idle",
                              rate=rate)
    if kind == "tcp-300":
        return load.tcp_storm(t4, bed.P_HOLD, 2 * rate * secs, secs, "idle",
                              rate=2 * rate)
    if kind == "tcp-fill":
        return load.tcp_storm(t4, bed.P_HOLD, rate * secs, secs, "push",
                              rate=rate)
    if kind == "mixed":
        out = {}
        t = threading.Thread(target=lambda: out.update(
            load.udp_flows(t4, bed.P_SINK, rate, secs)))
        t.start()
        out.update(load.tcp_storm(t4, bed.P_HOLD, rate * secs, secs,
                                  "idle", rate=rate))
        t.join()
        return out
    if kind == "bursts":
        return bursts(bed, 2 * rate, secs)
    if kind == "no-answer":
        return load.tcp_storm(NO_ANSWER, 80, rate * secs, secs, "idle",
                              rate=rate, connect_timeout=1.0)
    raise ValueError(kind)


class Live:
    """The long-lived flows, run for the whole measurement."""

    def __init__(self, bed, keepalives=(), handshake_ms=None, page=50,
                 first_ms=None):
        self.bed = bed
        self.handshake_ms = handshake_ms
        self.first_ms = first_ms
        self.page = page
        # outcomes of new TCP connections during the surge: answered,
        # reset at once (refused), reset later (evicted before the answer),
        # timed out
        self.outcomes = {"handshake": collections.Counter(),
                         "page": collections.Counter()}
        self.stop = threading.Event()
        self.surging = threading.Event()
        self.after = threading.Event()
        self.call = [0, 0]
        self.ka = {"udp": [0, 0, 0], "tcp": [0, 0]}
        self.extra = {}
        self.probe = {"tcp": [0, 0], "udp": [0, 0]}
        self.shake = [0, 0]
        self.pages = {"surge": [], "after": []}
        self.page_jobs = []
        self.down = [0, 0.0]
        self.jobs = [threading.Thread(target=f, daemon=True) for f in
                     (self.call_flow, self.udp_keepalive, self.tcp_keepalive,
                      self.probes, self.handshakes, self.page_loads,
                      self.download)]
        for count, every in keepalives:
            self.extra[every] = [0, 0, 0]
            for i in range(count):
                self.jobs.append(threading.Thread(
                    target=self.udp_keepalive, daemon=True,
                    args=(every, self.extra[every], every * i / count)))

    def start(self):
        self.idle = client.tcp_connect(self.bed.target4, self.bed.P_ECHO)
        self.idle.sendall(b"i")
        client.recv_exact(self.idle, 1)
        for j in self.jobs:
            j.start()

    def finish(self):
        self.stop.set()
        for j in self.jobs:
            j.join()
        for j in self.page_jobs:
            j.join()
        self.idle.settimeout(3)
        try:
            self.idle.sendall(b"i")
            idle_ok = client.recv_exact(self.idle, 1) == b"i"
        except OSError:
            idle_ok = False
        self.idle.close()
        return idle_ok

    def call_flow(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(0.02)
        s.connect((self.bed.target4, self.bed.P_ECHO))
        while not self.stop.is_set():
            counting = self.surging.is_set()
            try:
                s.send(b"c" * 160)
                s.recv(2048)
                got = 1
            except OSError:
                got = 0
            if counting:
                self.call[0] += 1
                self.call[1] += got
            time.sleep(0.02)
        s.close()

    def udp_keepalive(self, every=4.0, counts=None, phase=0.0):
        counts = self.ka["udp"] if counts is None else counts
        if self.stop.wait(phase):
            return
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(1.0)
        s.connect((self.bed.target4, self.bed.P_PEER))
        peer = None
        while not self.stop.is_set():
            try:
                s.send(b"k")
                got = s.recv(2048)
            except OSError:
                got = None
            if peer is None:
                peer = got
            else:
                counts[0] += 1
                counts[1] += got == peer
                counts[2] += got is not None and got != peer
            self.stop.wait(every)
        s.close()

    def tcp_keepalive(self):
        s = client.tcp_connect(self.bed.target4, self.bed.P_ECHO)
        s.settimeout(2.0)
        ok = True
        while not self.stop.wait(4.0):
            if ok:
                try:
                    s.sendall(b"k")
                    ok = s.recv(1) == b"k"
                except OSError:
                    ok = False
            self.ka["tcp"][0] += 1
            self.ka["tcp"][1] += ok
        s.close()

    def probes(self):
        while not self.stop.wait(1.0):
            if not self.surging.is_set():
                continue
            self.probe["tcp"][0] += 1
            try:
                self.probe["tcp"][1] += client.tcp_echo(
                    self.bed.target4, self.bed.P_ECHO, 1000, 2.0)
            except OSError:
                pass
            self.probe["udp"][0] += 1
            try:
                self.probe["udp"][1] += client.udp_echo(
                    self.bed.target4, self.bed.P_ECHO, b"p", 1.0, 1) is not None
            except OSError:
                pass

    def slow_exchange(self):
        """answered, refused, evicted or timeout."""
        ms = random.uniform(*(self.handshake_ms or (300, 800)))
        if self.first_ms:
            req, want = b"t%d,%d" % (self.first_ms, ms - self.first_ms), b"ab"
        else:
            req, want = b"d%d" % ms, None
        want = want or req
        t0 = time.monotonic()
        got = b""
        try:
            s = client.tcp_connect(self.bed.target4, self.bed.P_SLOW, 3.0)
            try:
                s.settimeout(3.0 + ms / 1000.0)
                s.sendall(req)
                while len(got) < len(want):
                    d = s.recv(len(want) - len(got))
                    if not d:
                        raise ConnectionResetError
                    got += d
            finally:
                s.close()
        except ConnectionError:
            return "refused" if time.monotonic() - t0 < 0.1 else "evicted"
        except OSError:
            return "timeout"
        return "answered" if got == want else "evicted"

    def handshakes(self):
        def one():
            r = self.slow_exchange()
            self.shake[1] += r == "answered"
            self.outcomes["handshake"][r] += 1

        while not self.stop.wait(0.1):
            if self.surging.is_set():
                self.shake[0] += 1
                threading.Thread(target=one, daemon=True).start()

    def page_loads(self):
        def page(out, surging):
            done = []
            t0 = time.monotonic()
            jobs = [threading.Thread(
                target=lambda: done.append(self.slow_exchange()))
                for _ in range(self.page)]
            for j in jobs:
                j.start()
            for j in jobs:
                j.join()
            out.append((all(d == "answered" for d in done),
                        time.monotonic() - t0))
            if surging:
                self.outcomes["page"].update(done)

        while not self.stop.wait(5.0):
            if self.surging.is_set():
                out = self.pages["surge"]
            elif self.after.is_set():
                out = self.pages["after"]
            else:
                continue
            # finish() waits for it, so the tunnel isn't stopped under it
            j = threading.Thread(target=page, daemon=True,
                                 args=(out, self.surging.is_set()))
            self.page_jobs.append(j)
            j.start()

    def download(self):
        try:
            s = client.tcp_connect(self.bed.target4, self.bed.P_SOURCE)
        except OSError:
            return
        s.settimeout(1.0)
        while not self.stop.is_set():
            try:
                n = len(s.recv(1 << 16))
            except socket.timeout:
                continue
            except OSError:
                break
            if not n:
                break
            if self.surging.is_set():
                self.down[0] += n
        s.close()


def recovery(bed, limit=10.0):
    """Seconds until a new TCP and UDP exchange both work again."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < limit:
        try:
            if (client.tcp_echo(bed.target4, bed.P_ECHO, 100, 1.0) and
                    client.udp_echo(bed.target4, bed.P_ECHO, b"r", 0.5, 1)):
                return time.monotonic() - t0
        except OSError:
            pass
        time.sleep(0.1)
    return None


def netem(bed, spec):
    """Delay and loss both ways between the tunnel and the server (Linux)."""
    for ns, dev in ((bed.ns_c, "hvc0"), (bed.ns_s, "hvs0")):
        argv = ["ip", "netns", "exec", ns, "tc", "qdisc"]
        if spec:
            argv += ["replace", "dev", dev, "root", "netem"] + spec.split()
        else:
            argv += ["del", "dev", dev, "root"]
        subprocess.run(argv, check=bool(spec), capture_output=True)


def crowd_cpu(proc, cpu):
    """Pin the tunnel to ``cpu`` and keep that CPU busy."""
    subprocess.run(["taskset", "-a", "-p", "-c", str(cpu), str(proc.pid)],
                   check=True, capture_output=True)
    return subprocess.Popen(["taskset", "-c", str(cpu), sys.executable, "-c",
                             "while True: pass"])


def measure(bed, misc, kind, st):
    live = Live(bed, st["keepalives"], st["handshake_ms"], st["page"],
                st["first_ms"])
    hog = None
    if st["netem"]:
        netem(bed, st["netem"])
    try:
        with bl.running(bed, mem_limit=st["mem_mib"] * MiB,
                        misc=misc) as (proc, _):
            if st["cpu"] is not None:
                hog = crowd_cpu(proc, st["cpu"])
            try:
                live.start()
                time.sleep(st["warm"])
                live.surging.set()
                t0 = time.monotonic()
                sg = surge(bed, kind, st)
                secs = time.monotonic() - t0
                live.surging.clear()
                live.after.set()
                rec = recovery(bed)
                time.sleep(st["after"])
                mem_after = bed.memcg_stats(proc).get("current", 0)
                idle_ok = live.finish()
            finally:
                live.stop.set()
                if hog:
                    hog.kill()
                    hog.wait()
            mem = bed.memcg_stats(proc)
            alive = proc.alive() and not mem.get("events_oom_kill")
    finally:
        if st["netem"]:
            netem(bed, None)

    def pct(a):
        return 100.0 * a[1] / a[0] if a[0] else None

    def pages(p):
        times = [t for ok, t in p if ok]
        return (pct([len(p), len(times)]),
                statistics.median(times) if times else None)

    r = {
        "survived": alive,
        "peak_mib": mem.get("peak", 0) / MiB,
        "after_mib": mem_after / MiB,
        "call_loss_pct": 100.0 - (pct(live.call) or 0.0),
        "keepalive_udp_kept_pct": pct(live.ka["udp"]),
        "keepalive_udp_moved_pct": pct([live.ka["udp"][0],
                                        live.ka["udp"][2]]),
        "keepalive_tcp_kept_pct": pct(live.ka["tcp"]),
        "idle_tcp_kept": idle_ok,
        "download_mbps": bl.mbps(live.down[0], secs),
        "probe_tcp_pct": pct(live.probe["tcp"]),
        "probe_udp_pct": pct(live.probe["udp"]),
        "handshake_pct": pct(live.shake),

        "recovery_s": rec,
        "surge": sg,
    }
    (r["page_surge_pct"], r["page_surge_s"]) = pages(live.pages["surge"])
    (r["page_after_pct"], r["page_after_s"]) = pages(live.pages["after"])
    for what, c in live.outcomes.items():
        n = sum(c.values())
        for outcome in ("refused", "evicted", "timeout"):
            r["%s_%s_pct" % (what, outcome)] = pct([n, c[outcome]])
    for every, c in live.extra.items():
        r["keepalive_%gs_kept_pct" % every] = pct(c)
        r["keepalive_%gs_moved_pct" % every] = pct([c[0], c[2]])
    return r


def run(bed, rep, args):
    if not bed.memcg_supported():
        rep.group("surge: skipped, memory limits are not available")
        return
    bl.raise_nofile()
    st = setup()
    for name, misc in st["configs"].items():
        group = "surge, %s" % name
        rep.group(group)
        for kind in st["surges"]:
            r = measure(bed, misc, kind, st)
            if st["out"]:
                with open(st["out"], "a") as f:
                    f.write(json.dumps(dict(r, config=name, kind=kind,
                                            setup=st)) + "\n")
            tag = kind + " "
            for k, v in r.items():
                if v is None or k == "surge":
                    continue
                unit = ("%" if k.endswith("_pct") else
                        "MiB" if k.endswith("_mib") else
                        "s" if k.endswith("_s") else
                        "Mbit/s" if k.endswith("_mbps") else "")
                name_ = k.rsplit("_", 1)[0] if unit else k
                rep.add(group, tag + name_.replace("_", " "), float(v), unit)
