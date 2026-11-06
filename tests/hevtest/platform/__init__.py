"""Platform back-ends.

A back-end knows how to build an isolated network testbed on its OS and how
to run the tunnel inside it.  To add a platform (Windows, FreeBSD, ...) create
a module here with a ``Bed`` class deriving from ``base.Bed`` and register it
in ``_BACKENDS``.
"""
import sys

_BACKENDS = {
    "linux": "hevtest.platform.linux",
    # "freebsd": "hevtest.platform.freebsd",
    # "win32": "hevtest.platform.windows",
}


def get_bed_class(name=None):
    import importlib
    name = name or sys.platform
    for prefix, mod in _BACKENDS.items():
        if name.startswith(prefix):
            return importlib.import_module(mod).Bed
    raise SystemExit("hevtest: platform '%s' is not supported yet" % name)
