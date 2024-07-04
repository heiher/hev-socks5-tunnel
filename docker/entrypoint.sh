#!/bin/sh

TUN="${TUN:-tun0}"
MTU="${MTU:-8500}"
IPV4="${IPV4:-198.18.0.1}"
SOCKS5_ADDR="${SOCKS5_ADDR:-192.168.0.1}"
SOCKS5_PORT="${SOCKS5_PORT:-1080}"
SOCKS5_UDP_MODE="${SOCKS5_UDP_MODE:-udp}"

TABLE="${TABLE:-20}"
MARK="${MARK:-438}"

LOG_LEVEL="${LOG_LEVEL:-warn}"

config_file() {
  cat > /hs5t.yml << EOF
misc:
  log-level: '${LOG_LEVEL}'
tunnel:
  name: '${TUN}'
  mtu: ${MTU}
  ipv4: '${IPV4}'
  ipv6: '${IPV6}'
  post-up-script: '/route.sh'
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
  echo "#!/bin/sh" > /route.sh
  chmod +x /route.sh

  echo "ip route add default dev ${TUN} table ${TABLE}" >> /route.sh

  for addr in $(echo ${IPV4_INCLUDED_ROUTES} | tr ',' '\n'); do
    echo "ip rule add to ${addr} table ${TABLE}" >> /route.sh
  done

  for addr in $(echo ${IPV4_EXCLUDED_ROUTES} | tr ',' '\n'); do
    echo "ip rule add to ${addr} table main" >> /route.sh
  done

  echo "ip rule add fwmark 0x${MARK} table main pref 1" >> /route.sh
}

run() {
  config_file
  config_route
  hev-socks5-tunnel /hs5t.yml
}

run || exit 1
