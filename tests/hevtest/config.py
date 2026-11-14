"""Tunnel configuration builder (no YAML dependency)."""
import copy

MiB = 1 << 20

# What a 50 MiB budget (iOS NE) is configured with, see README.
LOW_MEMORY = {
    "max-session-count": 200,
    "max-buffer-size": 4 * MiB,
    "tcp-buffer-size": 16384,
    "task-stack-size": 36864,
    "tcp-socket-buffer-size": 32768,
    "udp-recv-buffer-size": 65536,
}


def _scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return str(v)
    return "'%s'" % str(v).replace("'", "''")


def dump(cfg):
    out = []
    for sect, body in cfg.items():
        out.append("%s:" % sect)
        for k, v in body.items():
            if v is None:
                continue
            out.append("  %s: %s" % (k, _scalar(v)))
    return "\n".join(out) + "\n"


def default(bed):
    return {
        "tunnel": {
            "name": bed.tun_name,
            "mtu": 8500,
            "ipv4": bed.tun_ipv4,
            "ipv6": bed.tun_ipv6,
        },
        "socks5": {
            "port": bed.socks_port,
            "address": bed.socks_addr,
            "udp": "udp",
            "mark": bed.mark,
        },
        "misc": {
            "log-file": "stderr",
            "log-level": "warn",
        },
    }


def build(bed, **over):
    """``build(bed, tunnel={'mtu': 1500}, mapdns={...})``; a value of None
    removes the key, a section of None removes the section."""
    cfg = default(bed)
    for sect, body in over.items():
        if body is None:
            cfg.pop(sect, None)
            continue
        cfg.setdefault(sect, {}).update(copy.deepcopy(body))
    return cfg
