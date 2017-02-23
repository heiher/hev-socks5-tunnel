# HevSocks5Tunnel

[![status](https://gitlab.com/hev/hev-socks5-tunnel/badges/master/pipeline.svg)](https://gitlab.com/hev/hev-socks5-tunnel/commits/master)

A tunnel over Socks5 proxy.

**Features**
* Redirect TCP connections.
* Redirect DNS queries. (see [server](https://gitlab.com/hev/hev-socks5-server))
* IPv4/IPv6. (dual stack)

## How to Build

**Linux**:
```bash
git clone git://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
git submodule init
git submodule update
make
```

## How to Use

### Config

```yaml
tunnel:
  # Interface name
  name: tun0
  # Interface MTU
  mtu: 8192
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
  # Domain name service
  dns:
    port: 53

socks5:
  # Socks5 server port
  port: 1080
  # Socks5 server address (ipv4/ipv6)
  address: 127.0.0.1

#misc:
   # null, stdout, stderr or file-path
#  log-file: null
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
```

## Authors
* **Heiher** - https://hev.cc

## License
LGPL
