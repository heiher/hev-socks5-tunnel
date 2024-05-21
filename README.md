# HevSocks5Tunnel

[![status](https://github.com/heiher/hev-socks5-tunnel/actions/workflows/build.yaml/badge.svg?branch=master&event=push)](https://github.com/heiher/hev-socks5-tunnel)

A tunnel over Socks5 proxy (tun2socks) for Unix.

## Features

* IPv4/IPv6. (dual stack)
* Redirect TCP connections.
* Redirect UDP packets. (Fullcone NAT, UDP in UDP/TCP)
* Linux/Android/FreeBSD/macOS/iOS/WSL2.

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

### iOS and MacOS

```bash
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel
# will generate HevSocks5Tunnel.xcframework
./build.sh
```

### Library

```bash
git clone --recursive https://github.com/heiher/hev-socks5-tunnel
cd hev-socks5-tunnel

# Static library
make static

# Shared library
make shared

# Static library for iOS
make PP="xcrun --sdk iphoneos --toolchain iphoneos clang" \
     CC="xcrun --sdk iphoneos --toolchain iphoneos clang" \
     CFLAGS="-arch arm64 -mios-version-min=12.0" \
     LFLAGS="-arch arm64 -mios-version-min=12.0 -Wl,-Bsymbolic-functions" static

libtool -static -o libhev-socks5-tunnel.a \
                   bin/libhev-socks5-tunnel.a \
                   third-part/lwip/bin/liblwip.a \
                   third-part/yaml/bin/libyaml.a \
                   third-part/hev-task-system/bin/libhev-task-system.a
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
  # Socks5 handshake using pipeline mode
# pipeline: false
  # Socks5 server username
# username: 'username'
  # Socks5 server password
# password: 'password'
  # Socket mark
# mark: 0

#misc:
   # task stack size (bytes)
#  task-stack-size: 81920
   # connect timeout (ms)
#  connect-timeout: 5000
   # read-write timeout (ms)
#  read-write-timeout: 60000
   # stdout, stderr or file-path
#  log-file: stderr
   # debug, info, warn or error
#  log-level: warn
   # If present, run as a daemon with this pid file
#  pid-file: /run/hev-socks5-tunnel.pid
   # If present, set rlimit nofile; else use default value
#  limit-nofile: 65535
```

### Run

#### Linux

```bash
# Set socks5.mark = 0x438
bin/hev-socks5-tunnel conf/main.yml

# Bypass upstream socks5 server
sudo ip rule add fwmark 0x438 lookup main pref 10
sudo ip -6 rule add fwmark 0x438 lookup main pref 10

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
sudo route change -inet default -interface utun99
sudo route change -inet6 default -interface utun99
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
* **EbrahimTahernejad** - https://github.com/EbrahimTahernejad
* **heiby** - https://github.com/heiby
* **hev** - https://hev.cc
* **pronebird** - https://github.com/pronebird

## License

MIT
