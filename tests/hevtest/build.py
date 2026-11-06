"""Build the tunnel (if needed) and the test tools."""
import glob
import os
import subprocess
import sys


def _run(argv, cwd):
    r = subprocess.run(argv, cwd=cwd)
    if r.returncode != 0:
        raise SystemExit("hevtest: build failed: %s" % " ".join(argv))


def ensure_tunnel(root, path=None, jobs=None):
    if path:
        if not os.access(path, os.X_OK):
            raise SystemExit("hevtest: %s is not executable" % path)
        return os.path.abspath(path)
    _run(["make", "-j%d" % (jobs or os.cpu_count() or 1)], root)
    return os.path.join(root, "bin", "hev-socks5-tunnel")


def ensure_tools(root, outdir):
    """Compile tools/socks5d.c against HevSocks5Core + HevTaskSystem."""
    os.makedirs(outdir, exist_ok=True)
    out = os.path.join(outdir, "socks5d")
    srcs = [os.path.join(root, "tests", "tools", "socks5d.c")]
    srcs += sorted(glob.glob(os.path.join(root, "src", "core", "src", "*.c")))
    lib = os.path.join(root, "third-part", "hev-task-system", "bin",
                       "libhev-task-system.a")
    if not os.path.exists(lib):
        raise SystemExit("hevtest: %s missing, build the project first" % lib)
    if os.path.exists(out):
        newest = max(os.path.getmtime(p) for p in srcs + [lib])
        if os.path.getmtime(out) >= newest:
            return outdir
    cc = os.environ.get("CC", "cc")
    _run([cc, "-O2", "-Wall", "-pipe",
          "-I" + os.path.join(root, "src", "core", "include"),
          "-I" + os.path.join(root, "third-part", "hev-task-system", "include"),
          ] + srcs + [lib, "-lpthread", "-o", out], root)
    return outdir
