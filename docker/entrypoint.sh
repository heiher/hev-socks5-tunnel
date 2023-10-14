#!/bin/sh

TUN="${TUN:-tun0}"
MTU="${MTU:-8500}"
NET="${NET:-172.17.0.0/16}"
IPV4="${IPV4:-198.18.0.1}"
SOCKS5_ADDR="${SOCKS5_ADDR:-192.168.0.1}"
SOCKS5_PORT="${SOCKS5_PORT:-1080}"
SOCKS5_UDP_MODE="${SOCKS5_UDP_MODE:-udp}"

TABLE="${TABLE:-20}"
MARK="${MARK:-438}"

config_file() {
  cat > /hs5t.yml << EOF
tunnel:
  name: '${TUN}'
  mtu: ${MTU}
  ipv4: '${IPV4}'
  ipv6: '${IPV6}'
socks5:
  port: ${SOCKS5_PORT}
  address: '${SOCKS5_ADDR}'
  udp: '${SOCKS5_UDP_MODE}'
  mark: ${MARK}
EOF

  if [ -n "${SOCKS5_USERNAME}" ]; then
      echo "  username: '${SOCKS5_USERNAME}'" >> /hs5t.yml
  fi

  if [ -n "${SOCKS5_PASSWORD}" ]; then
      echo "  password: '${SOCKS5_PASSWORD}'" >> /hs5t.yml
  fi
}

config_route() {
  ip route flush table ${TABLE} > /dev/null 2>&1
  ip route add default dev ${TUN} table ${TABLE}
  ip rule delete pref 10 > /dev/null 2>&1
  ip rule delete pref 11 > /dev/null 2>&1
  ip rule delete pref 12 > /dev/null 2>&1
  ip rule delete pref 20 > /dev/null 2>&1
  ip rule add fwmark 0x${MARK} lookup main pref 10
  ip rule add from ${NET} lookup main pref 11
  ip rule add to ${NET} lookup main pref 12
  ip rule add lookup ${TABLE} pref 20
}

run() {
  config_file
  hev-socks5-tunnel /hs5t.yml &
  PID=$!
  config_route
  wait ${PID}
}

run || exit 1
