"""Command line driver:  run.sh func|bench|all|list [options]."""
import argparse
import importlib
import os
import platform as _platform
import sys
import time
import unittest

from . import build
from .platform import get_bed_class

TESTS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT = os.path.dirname(TESTS_DIR)


def parse(argv):
    p = argparse.ArgumentParser(
        prog="run.sh", description="hev-socks5-tunnel black-box test suite")
    p.add_argument("mode", choices=["func", "bench", "all", "list"],
                   help="func: functional tests; bench: benchmarks; "
                        "all: both (separate sessions); list: show tests")
    p.add_argument("-b", "--bin", help="tunnel binary (default: build with make)")
    p.add_argument("-k", dest="pattern", action="append", default=[],
                   help="only run tests whose name matches (substring, "
                        "repeatable)")
    p.add_argument("-d", "--duration", type=float,
                   default=float(os.environ.get("BENCH_DURATION", "3")),
                   help="seconds per throughput measurement (default 3)")
    p.add_argument("-x", "--failfast", action="store_true")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--no-build", action="store_true",
                   help="do not run make for the tunnel (needs --bin or bin/)")
    return p.parse_args(argv)


def functional_suite(patterns):
    loader = unittest.TestLoader()
    if patterns:
        loader.testNamePatterns = ["*%s*" % x for x in patterns]
    return loader.discover(os.path.join(TESTS_DIR, "functional"),
                           pattern="test_*.py", top_level_dir=TESTS_DIR)


def bench_modules(patterns):
    d = os.path.join(TESTS_DIR, "bench")
    mods = []
    order = ["bench_tcp", "bench_udp", "bench_latency", "bench_sessions"]
    files = sorted(os.listdir(d), key=lambda f: (
        order.index(f[:-3]) if f[:-3] in order else len(order), f))
    for f in files:
        if f.startswith("bench_") and f.endswith(".py"):
            name = f[:-3]
            if patterns and not any(x in name for x in patterns):
                continue
            mods.append(importlib.import_module("bench." + name))
    return mods


def iter_tests(suite):
    for t in suite:
        if isinstance(t, unittest.TestSuite):
            yield from iter_tests(t)
        else:
            yield t


def make_bed(args):
    tunnel = args.bin
    if not tunnel and args.no_build:
        tunnel = os.path.join(ROOT, "bin", "hev-socks5-tunnel")
    tunnel = build.ensure_tunnel(ROOT, tunnel)
    tools = build.ensure_tools(ROOT, os.path.join(TESTS_DIR, ".build"))
    Bed = get_bed_class()
    return Bed(tunnel, tools, verbose=args.verbose)


class Session:
    """Testbed + shared servers for one run."""

    def __init__(self, args):
        self.bed = make_bed(args)

    def __enter__(self):
        b = self.bed
        b.setup()
        b.start_targets()
        b.start_socks()
        b.enter_client()
        return b

    def __exit__(self, *exc):
        self.bed.teardown()


class TimedResult(unittest.TextTestResult):
    """Shows the slowest tests at the end."""
    times = []

    def startTest(self, test):
        self._t0 = time.time()
        super().startTest(test)

    def stopTest(self, test):
        TimedResult.times.append((time.time() - self._t0, test.id()))
        super().stopTest(test)


def run_func(args):
    from .case import TunnelTestCase
    suite = functional_suite(args.pattern)
    if suite.countTestCases() == 0:
        print("no functional tests selected")
        return 1
    with Session(args) as bed:
        TunnelTestCase.bed = bed
        print("== functional tests (%s, %s) ==" % (bed.name, bed.tunnel_bin),
              flush=True)
        runner = unittest.TextTestRunner(verbosity=2 if args.verbose else 1,
                                         failfast=args.failfast,
                                         resultclass=TimedResult)
        res = runner.run(suite)
        slow = sorted(TimedResult.times, reverse=True)[:5]
        print("slowest: " + ", ".join("%s %.1fs" % (n.rsplit(".", 1)[-1], t)
                                      for t, n in slow))
    return 0 if res.wasSuccessful() else 1


def run_bench(args):
    from .report import Report
    # long ones run only when asked for by name
    mods = [m for m in bench_modules(args.pattern)
            if args.pattern or not getattr(m, "ON_REQUEST", False)]
    if not mods:
        print("no benchmarks selected")
        return 1
    with Session(args) as bed:
        rep = Report({
            "platform": "%s %s" % (_platform.system(), _platform.release()),
            "cpus": os.cpu_count(),
            "binary": bed.tunnel_bin,
            "duration_s": args.duration,
        })
        print("== benchmarks (%s) ==" % bed.name)
        failed = 0
        for m in mods:
            try:
                m.run(bed, rep, args)
            except Exception as e:  # keep going, report at the end
                import traceback
                traceback.print_exc()
                print("benchmark %s FAILED: %s" % (m.__name__, e))
                failed += 1
    rep.write_step_summary()
    return 1 if failed else 0


def raise_nofile(want=1048576):
    """Children (socks5d, targets, tunnel) inherit this; the default 1024 is
    too low for the many-sessions benchmark and tests."""
    import resource
    soft, hard = resource.getrlimit(resource.RLIMIT_NOFILE)
    new = want if hard == resource.RLIM_INFINITY else min(want, hard)
    if soft == resource.RLIM_INFINITY or soft >= new:
        return
    try:
        resource.setrlimit(resource.RLIMIT_NOFILE, (new, hard))
    except (ValueError, OSError):
        pass


def main(argv=None):
    args = parse(argv if argv is not None else sys.argv[1:])
    raise_nofile()
    sys.path.insert(0, TESTS_DIR)
    if args.mode == "list":
        for t in iter_tests(functional_suite(args.pattern)):
            print("func   ", t.id().split(".", 1)[-1])
        for m in bench_modules(args.pattern):
            print("bench  ", m.__name__.split(".")[-1])
        return 0
    rc = 0
    if args.mode in ("func", "all"):
        rc |= run_func(args)
    if args.mode in ("bench", "all"):
        rc |= run_bench(args)
    return rc


if __name__ == "__main__":
    sys.exit(main())
