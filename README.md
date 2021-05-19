# HevSocks5Tunnel

[![status](https://gitlab.com/hev/hev-socks5-tunnel/badges/master/pipeline.svg)](https://gitlab.com/hev/hev-socks5-tunnel/commits/master)

A tunnel over Socks5 proxy.

**Features**
* IPv4/IPv6. (dual stack)
* Redirect TCP connections.
* Redirect UDP packets. (UDP over TCP see [server](https://gitlab.com/hev/hev-socks5-server))

## How to Build

**Linux**:
```bash
git clone --recursive git://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
make
```

**Android**:
```bash
mkdir hev-socks5-tunnel
cd hev-socks5-tunnel
git clone --recursive git://github.com/heiher/hev-socks5-tunnel jni
ndk-build
```

## How to Use

### Config

```yaml
tunnel:
  # Interface name
  name: tun0
  # Interface MTU
  mtu: 10496
  # IPv4 address
  ipv4:
    address: 10.0.0.2
    gateway: 10.0.0.1
    prefix: 30
  # IPv6 address
  ipv6:
    address: 'fc00::2'
    gateway: 'fc00::1'
    prefix: 126

socks5:
  # Socks5 server port
  port: 1080
  # Socks5 server address (ipv4/ipv6)
  address: 127.0.0.1

#misc:
   # stdout, stderr or file-path
#  log-file: stderr
   # debug, info, warn or error
#  log-level: warn
   # If present, run as a daemon with this pid file
#  pid-file: /run/hev-socks5-tunnel.pid
   # If present, set rlimit nofile; else use default value
#  limit-nofile: 1024
```

### Run

```bash
bin/hev-socks5-tunnel conf/main.yml

# Bypass upstream socks5 server
sudo ip route add SOCKS5_SERVER dev DEFAULT_IFACE metric 10

# Route others
sudo ip route add default dev tun0 metric 20
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL
