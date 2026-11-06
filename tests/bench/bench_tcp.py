"""TCP throughput and tunnel CPU cost."""
from hevtest import benchlib as bl


def run(bed, rep, args):
    D = args.duration
    cases = [
        ("ipv4", bed.target4, 1), ("ipv4", bed.target4, 8),
        ("ipv6", bed.target6, 1), ("ipv6", bed.target6, 8),
    ]
    with bl.running(bed) as (proc, smp):
        for direction in ("upload", "download"):
            rep.group("tcp %s" % direction)
            for fam, host, n in cases:
                smp.start()
                if direction == "upload":
                    nbytes, secs = bl.tcp_upload(host, bed.P_COUNT, D, n)
                else:
                    nbytes, secs = bl.tcp_download(host, bed.P_SOURCE, D, n)
                st = smp.stop()
                tag = "%s x%d" % (fam, n)
                rep.add("tcp %s" % direction, "throughput " + tag,
                        bl.mbps(nbytes, secs), "Mbit/s")
                rep.add("tcp %s" % direction, "tunnel cpu " + tag,
                        st["cpu_pct"], "%")
                if nbytes:
                    rep.add("tcp %s" % direction, "cpu per GB " + tag,
                            st["cpu_s"] / (nbytes / 1e9), "s/GB")
        st = smp.stop()
        rep.group("tcp memory")
        rep.add("tcp memory", "tunnel peak RSS", st["peak_rss_kb"] / 1024.0,
                "MiB")
