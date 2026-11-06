"""Benchmark result collection and rendering."""
import os


class Report:
    def __init__(self, meta=None):
        self.meta = meta or {}
        self.rows = []          # (group, name, value, unit)

    def add(self, group, name, value, unit=""):
        self.rows.append((group, name, value, unit))
        print("  %-34s %12s %s" % (name, _fmt(value), unit), flush=True)

    def group(self, title):
        print("\n[%s]" % title, flush=True)

    def markdown(self):
        out = ["### hev-socks5-tunnel benchmark", ""]
        for k, v in self.meta.items():
            out.append("- %s: `%s`" % (k, v))
        group = None
        for g, n, v, u in self.rows:
            if g != group:
                group = g
                out += ["", "**%s**" % g, "", "| metric | value | unit |",
                        "|---|---:|---|"]
            out.append("| %s | %s | %s |" % (n, _fmt(v), u))
        return "\n".join(out) + "\n"

    def write_step_summary(self):
        p = os.environ.get("GITHUB_STEP_SUMMARY")
        if p:
            with open(p, "a") as f:
                f.write(self.markdown())


def _fmt(v):
    if isinstance(v, float):
        return "%.2f" % v if v < 1000 else "%.0f" % v
    return str(v)
