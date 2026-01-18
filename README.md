# HevSocks5Tunnel

[![status](https://github.com/heiher/hev-socks5-tunnel/actions/workflows/build.yaml/badge.svg?branch=main&event=push)](https://github.com/heiher/hev-socks5-tunnel)

A simple, lightweight tunnel over Socks5 proxy (tun2socks).

## Features

* IPv4/IPv6. (dual stack)
* Redirect TCP connections.
* Redirect UDP packets. (Fullcone NAT, UDP-in-UDP and UDP-in-TCP [^1])
* Linux/Android/FreeBSD/macOS/iOS/Windows.

## Benchmarks

See [here](https://github.com/heiher/hev-socks5-tunnel/wiki/Benchmarks) for more details.

### Speed

![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/upload-speed.png)
![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/download-speed.png)

### CPU usage

![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/upload-cpu.png)
![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/download-cpu.png)

### Memory usage

![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/upload-mem.png)
![](https://github.com/heiher/hev-socks5-tunnel/wiki/res/download-mem.png)

## How to Build

### Unix

```bash
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
make
```

### Android

```bash
mkdir hev-socks5-tunnel
cd hev-socks5-tunnel
git clone --recursive https://github.com/heiher/hev-socks5-tunnel jni
ndk-build
```

### iOS and macOS

```bash
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
# will generate HevSocks5Tunnel.xcframework
./build-apple.sh
```

### Windows (MSYS2)
```bash
export MSYS=winsymlinks:native
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
make
```

### Library

```bash
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel

# Static library
make static

# Shared library
make shared
```

## How to Use

### Config

```yaml
tunnel:
  # Interface name
  name: tun0
  # Interface MTU
  mtu: 8500
  # Multi-queue
  multi-queue: false
  # IPv4 address
  ipv4: 198.18.0.1
  # IPv6 address
  ipv6: 'fc00::1'
  # Post up script
# post-up-script: up.sh
  # Pre down script
# pre-down-script: down.sh

socks5:
  # Socks5 server port
  port: 1080
  # Socks5 server address (ipv4/ipv6)
  address: 127.0.0.1
  # Socks5 UDP relay mode (tcp|udp)
  udp: 'udp'
  # Override the UDP address provided by the Socks5 server (ipv4/ipv6)
# udp-address: ''
  # Socks5 handshake using pipeline mode
# pipeline: false
  # Socks5 server username
# username: 'username'
  # Socks5 server password
# password: 'password'
  # Socket mark
# mark: 0

#mapdns:
  # Mapped DNS address
# address: 198.18.0.2
  # Mapped DNS port
# port: 53
  # Mapped IP network base
# network: 100.64.0.0
  # Mapped IP network mask
# netmask: 255.192.0.0
  # Mapped DNS cache size
# cache-size: 10000

#misc:
  # task stack size (bytes)
# task-stack-size: 86016
  # tcp buffer size (bytes)
# tcp-buffer-size: 65536
  # udp socket recv buffer (SO_RCVBUF) size (bytes)
# udp-recv-buffer-size: 524288
  # number of udp buffers in splice, 1500 bytes per buffer.
# udp-copy-buffer-nums: 10
  # maximum session count (0: unlimited)
# max-session-count: 0
  # connect timeout (ms)
# connect-timeout: 10000
  # TCP read-write timeout (ms)
# tcp-read-write-timeout: 300000
  # UDP read-write timeout (ms)
# udp-read-write-timeout: 60000
  # stdout, stderr or file-path
# log-file: stderr
  # debug, info, warn or error
# log-level: warn
  # If present, run as a daemon with this pid file
# pid-file: /run/hev-socks5-tunnel.pid
  # If present, set rlimit nofile; else use default value
# limit-nofile: 65535
```

### Run

#### Linux

```bash
# Set socks5.mark = 438
bin/hev-socks5-tunnel conf/main.yml

# Disable reverse path filter
sudo sysctl -w net.ipv4.conf.all.rp_filter=0
sudo sysctl -w net.ipv4.conf.tun0.rp_filter=0

# Bypass upstream socks5 server
sudo ip rule add fwmark 438 lookup main pref 10
sudo ip -6 rule add fwmark 438 lookup main pref 10

# Route others
sudo ip route add default dev tun0 table 20
sudo ip rule add lookup 20 pref 20
sudo ip -6 route add default dev tun0 table 20
sudo ip -6 rule add lookup 20 pref 20
```

#### FreeBSD/macOS

```zsh
# Bypass upstream socks5 server
# 10.0.0.1: socks5 server
# 10.0.2.2: default gateway
sudo route add -net 10.0.0.1/32 10.0.2.2

# Route others
sudo route change -inet default -interface tun0
sudo route change -inet6 default -interface tun0
```

#### Windows

```zsh
# Bypass upstream socks5 server
# 10.0.0.1: socks5 server
# 10.0.2.2: default gateway
route add 10.0.0.1/32 10.0.2.2

# Route others
route change 0.0.0.0/0 0.0.0.0 if tun-index
route change ::/0 :: if tun-index
```

#### OpenWrt 24.10+

Repo: https://github.com/openwrt/packages/tree/master/net/hev-socks5-tunnel

```sh
# Install package
opkg install hev-socks5-tunnel

# Edit /etc/config/hev-socks5-tunnel

# Restart service
/etc/init.d/hev-socks5-tunnel restart
```

#### Low memory usage

On low-memory systems like iOS, reducing the size of the TCP buffer and
task stack, as well as limiting the maximum session count, can help prevent
out-of-memory issues.

```yaml
misc:
  # task stack size (bytes)
  task-stack-size: 24576 # 20480 + tcp-buffer-size
  # tcp buffer size (bytes)
  tcp-buffer-size: 4096
  # maximum session count
  max-session-count: 1200
```

#### Docker Compose

```yaml
version: "3.9"

services:
  client:
    image: alpine:latest # just for network testing
    tty: true # you can test network in terminal
    depends_on:
      tun:
        condition: service_healthy
    network_mode: "service:tun"

  tun:
    image: ghcr.io/heiher/hev-socks5-tunnel:latest # `latest` for the latest published version; `nightly` for the latest source build; `vX.Y.Z` for the specific version 
    cap_add:
      - NET_ADMIN # needed
    devices:
      - /dev/net/tun:/dev/net/tun # needed
    environment:
      TUN: tun0 # optional, tun interface name, default `tun0`
      MTU: 8500 # optional, MTU is MTU, default `8500`
      IPV4: 198.18.0.1 # optional, tun interface ip, default `198.18.0.1`
      TABLE: 20 # optional, ip route table id, default `20`
      MARK: 438 # optional, ip route rule mark, dec or hex format, default `438`
      SOCKS5_ADDR: a.b.c.d # socks5 proxy server address
      SOCKS5_PORT: 1080 # socks5 proxy server port
      SOCKS5_USERNAME: user # optional, socks5 proxy username, only set when need to auth
      SOCKS5_PASSWORD: pass # optional, socks5 proxy password, only set when need to auth
      SOCKS5_UDP_MODE: udp # optional, UDP relay mode, default `udp`, other option `tcp`
      SOCKS5_UDP_ADDR: a.b.c.d # optional, override the UDP address provided by the Socks5 server
      CONFIG_ROUTES: 1 # optional, set 0 to ignore TABLE, IPV4_INCLUDED_ROUTES and IPV4_EXCLUDED_ROUTES, with MARK defaults to 0
      IPV4_INCLUDED_ROUTES: 0.0.0.0/0 # optional, demo means proxy all traffic. for multiple network segments, join with `,` or `\n`
      IPV4_EXCLUDED_ROUTES: a.b.c.d # optional, demo means exclude traffic from the proxy itself. for multiple network segments, join with `,` or `\n`
      LOG_LEVEL: warn # optional, default `warn`, other option `debug`/`info`/`error`
    dns:
      - 8.8.8.8
```

You can also set the route rules with multiple network segments like:

```yaml
    environment:
      IPV4_INCLUDED_ROUTES: 10.0.0.0/8,172.16.0.0/12,192.168.0.0/16
      IPV4_EXCLUDED_ROUTES: |-
        a.b.c.d/24
        a.b.c.f/24
```

## API

```c
/**
 * hev_socks5_tunnel_main:
 * @config_path: config file path
 * @tun_fd: tunnel file descriptor
 *
 * Start and run the socks5 tunnel, this function will blocks until the
 * hev_socks5_tunnel_quit is called or an error occurs.
 *
 * Alias of hev_socks5_tunnel_main_from_file
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.4.6
 */
int hev_socks5_tunnel_main (const char *config_path, int tun_fd);

/**
 * hev_socks5_tunnel_main_from_file:
 * @config_path: config file path
 * @tun_fd: tunnel file descriptor
 *
 * Start and run the socks5 tunnel, this function will blocks until the
 * hev_socks5_tunnel_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_tunnel_main_from_file (const char *config_path, int tun_fd);

/**
 * hev_socks5_tunnel_main_from_str:
 * @config_str: string config
 * @config_len: the byte length of string config
 * @tun_fd: tunnel file descriptor
 *
 * Start and run the socks5 tunnel, this function will blocks until the
 * hev_socks5_tunnel_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_tunnel_main_from_str (const unsigned char *config_str,
                                     unsigned int config_len, int tun_fd);

/**
 * hev_socks5_tunnel_quit:
 *
 * Stop the socks5 tunnel.
 *
 * Since: 2.4.6
 */
void hev_socks5_tunnel_quit (void);

/**
 * hev_socks5_tunnel_stats:
 * @tx_packets (out): transmitted packets
 * @tx_bytes (out): transmitted bytes
 * @rx_packets (out): received packets
 * @rx_bytes (out): received bytes
 *
 * Retrieve tunnel interface traffic statistics.
 *
 * Since: 2.6.5
 */
void hev_socks5_tunnel_stats (size_t *tx_packets, size_t *tx_bytes,
                              size_t *rx_packets, size_t *rx_bytes);
```

## Use Cases

### Android VPN

* [SocksTun](https://github.com/heiher/sockstun)

### iOS

* [Tun2SocksKit](https://github.com/EbrahimTahernejad/Tun2SocksKit)

## Contributors

* **arror** - https://github.com/arror
* **bazuchan** - https://github.com/bazuchan
* **codewithtamim** - https://github.com/codewithtamim
* **dovecoteescapee** - https://github.com/dovecoteescapee
* **ebrahimtahernejad** - https://github.com/ebrahimtahernejad
* **heiby** - https://github.com/heiby
* **hev** - https://hev.cc
* **katana** - https://github.com/officialkatana
* **pronebird** - https://github.com/pronebird
* **saeeddev94** - https://github.com/saeeddev94
* **sskaje** - https://github.com/sskaje
* **wankkoree** - https://github.com/wankkoree
* **xz-dev** - https://github.com/xz-dev
* **yiguous** - https://github.com/yiguous
* **zheshinicheng** - https://github.com/zheshinicheng

## License

MIT

[^1]: See [protocol specification](https://github.com/heiher/hev-socks5-core/tree/main?tab=readme-ov-file#udp-in-tcp). The [hev-socks5-server](https://github.com/heiher/hev-socks5-server) supports UDP relay over TCP.
