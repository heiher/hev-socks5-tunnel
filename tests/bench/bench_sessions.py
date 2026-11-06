"""Connection setup rate and per-session memory."""
import time

from hevtest import benchlib as bl
from hevtest import client


def conn_rate(bed, host, secs, workers):
    end = time.time() + secs

    def one(_):
        n = 0
        while time.time() < end:
            s = client.tcp_connect(host, bed.P_ECHO, 10)
            s.sendall(b"x")
            s.recv(1)
            s.close()
            n += 1
        return n

    t0 = time.time()
    total = sum(bl.run_threads(workers, one))
    return total / (time.time() - t0)


def run(bed, rep, args):
    bl.raise_nofile()
    rep.group("sessions")
    # the default handshake is request/response; pipeline sends everything
    # in one go, so both are reported
    for pipe in (False, True):
        mode = "pipeline" if pipe else "default"
        with bl.running(bed, socks5={"pipeline": pipe}) as (proc, smp):
            t0 = time.perf_counter()
            s = client.tcp_connect(bed.target4, bed.P_ECHO, 10)
            s.sendall(b"x")
            s.recv(1)
            ttfb = (time.perf_counter() - t0) * 1e3
            s.close()
            rep.add("sessions", "new connection first byte (%s)" % mode, ttfb,
                    "ms")
            for workers in (1, 16):
                smp.start()
                rate = conn_rate(bed, bed.target4, args.duration, workers)
                st = smp.stop()
                rep.add("sessions", "connections/s x%d (%s)" % (workers, mode),
                        rate, "conn/s")
                rep.add("sessions", "tunnel cpu x%d (%s)" % (workers, mode),
                        st["cpu_pct"], "%")

    # idle sessions are opened in batches and each batch is confirmed before
    # the next one, so a slow machine cannot overrun the setup path.
    with bl.running(bed, misc={"limit-nofile": 65535}) as (proc, smp):
        base = bed.proc_stats(proc.pid).get("rss_kb", 0)
        n, batch = 1000, 100
        socks = []
        try:
            while len(socks) < n:
                new = []
                for _ in range(batch):
                    s = client.tcp_connect(bed.target4, bed.P_ECHO, 10)
                    s.settimeout(15)
                    s.sendall(b"x")
                    new.append(s)
                socks += new
                for s in new:
                    s.recv(1)
            held = bed.proc_stats(proc.pid).get("rss_kb", 0)
        except OSError as e:
            print("  %d of %d sessions established before: %s" %
                  (len(socks), n, e))
            print("  --- tunnel log ---\n" + proc.log()[-2000:])
            held = bed.proc_stats(proc.pid).get("rss_kb", 0)
        finally:
            count = len(socks)
            for s in socks:
                s.close()
        rep.add("sessions", "rss idle", base / 1024.0, "MiB")
        rep.add("sessions", "rss with %d sessions" % count, held / 1024.0,
                "MiB")
        if count:
            rep.add("sessions", "rss per session",
                    (held - base) / float(count), "KiB")
