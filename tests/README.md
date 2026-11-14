# Test suite

Black-box tests: the suite behaves like a normal user. It builds the tunnel,
starts it with a YAML config, and checks what applications see through the
public interface (config file, tun device, signals, sockets). No internal
symbols are touched.

```sh
tests/run.sh func              # functional tests, ~30 s, pass/fail
tests/run.sh bench             # benchmarks, report numbers only
tests/run.sh all               # both (two independent sessions)
tests/run.sh list              # list tests / benchmarks
```

Options: `-b BIN` use an existing binary (default: `make`), `-k NAME` select
tests/benchmarks by substring (repeatable), `-x` stop at first failure,
`-v` verbose, `-d SECONDS` per-measurement duration (default 3,
`BENCH_DURATION`). Results are printed as they are measured; when
`GITHUB_STEP_SUMMARY` is set the tables are also appended to the job summary.

Functional and benchmark runs are separate commands, so a CI job can run
them independently. The exit code is non-zero if a functional test fails.
Benchmarks only fail when they cannot run; numbers are reported, never
compared with history.

## Requirements (Ubuntu)

root (the script re-executes itself through `sudo`), Python >= 3.12, a C
compiler, `make`, `iproute2`, `ping` (`iputils-ping`) and `/dev/net/tun`.
Everything runs in throw-away network namespaces; nothing on the host is
reconfigured.

## Layout

```
tests/run.sh               entry point
tests/hevtest/             framework
  main.py                  CLI driver / discovery
  platform/base.py         platform-neutral testbed definition (topology)
  platform/linux.py        Linux back-end (netns + veth + policy routing)
  targets.py               echo/sink/source/... services behind the tunnel
  case.py config.py client.py benchlib.py report.py build.py
tests/tools/socks5d.c      SOCKS5 upstream (HevSocks5Core): auth, UDP, UDP-in-TCP
tests/functional/test_*.py unittest cases, one tunnel instance per test
tests/bench/bench_*.py     benchmarks: run(bed, report, args)
```

Topology (Linux): a *client* namespace holds `tun0` and the test process; a
*server* namespace holds `socks5d` and the target services. The target
addresses (`203.0.113.10`, `2001:db8::10`) exist only in the server
namespace, so a connection can succeed only if it really went through the
tunnel and the SOCKS5 server.

## Memory limits

`tests/functional/test_memory.py` runs the tunnel with a hard memory limit
(cgroup v2 `memory.max`, no swap: the kernel kills it beyond, as iOS does with
a Network Extension) and hits it with surges of thousands of connections from
`hevtest/load.py`. The tests are skipped when the memory controller is not
available to the root cgroup's children.

`tests/bench/bench_surge.py` measures, under the same limit, what happens to
long-lived flows (a call, keep-alives, an idle connection, a download) while
new connections keep coming, for each kind of surge. It takes about fifteen
minutes and runs only with `-k surge`; `HEVTEST_SURGE` (JSON, see the module)
picks the configs and surges and adds keep-alives, delay and loss, a busy CPU
or another memory limit. The tunnel is restarted for each surge, but the
targets are shared, so a surge that fills windows can slow the next
measurement down; run each kind on its own when comparing builds.

## Adding a platform

Create `hevtest/platform/<name>.py` with a `Bed` class deriving from
`base.Bed` (set up the two environments, `spawn_client/spawn_server`,
`start_tunnel`, `iface`, `proc_stats`, `ping`, `nofile_limit`) and register
it in `hevtest/platform/__init__.py`. Tests and benchmarks use only the
`Bed` API. Add an entry to the `Tests` matrix in `.github/workflows/ci.yml`.

## Adding a test

Functional: add `tests/functional/test_<x>.py`, derive from
`TunnelTestCase`, call `self.tunnel(section={...})` and use `hevtest.client`.
Benchmark: add `tests/bench/bench_<x>.py` with `run(bed, rep, args)` using
`benchlib.running(...)`, and register its name in `bench_modules()` order.
