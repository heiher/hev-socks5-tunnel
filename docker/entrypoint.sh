#!/bin/sh

TUN="${TUN:-tun0}"
MTU="${MTU:-8500}"
IPV4="${IPV4:-198.18.0.1}"
IPV6="${IPV6:-}"

CONFIG_ROUTES="${CONFIG_ROUTES:-1}"

TABLE="${TABLE:-20}"
if [ "${CONFIG_ROUTES}" == "0" ]; then
  MARK="${MARK:-0}"
else
  MARK="${MARK:-438}"
fi

SOCKS5_ADDR="${SOCKS5_ADDR:-172.17.0.1}"
SOCKS5_PORT="${SOCKS5_PORT:-1080}"
SOCKS5_USERNAME="${SOCKS5_USERNAME:-}"
SOCKS5_PASSWORD="${SOCKS5_PASSWORD:-}"
SOCKS5_UDP_MODE="${SOCKS5_UDP_MODE:-udp}"
SOCKS5_UDP_ADDR="${SOCKS5_UDP_ADDR:-}"

IPV4_INCLUDED_ROUTES="${IPV4_INCLUDED_ROUTES:-0.0.0.0/0}"
IPV4_EXCLUDED_ROUTES="${IPV4_EXCLUDED_ROUTES:-}"

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
  address: '${SOCKS5_ADDR}'
  port: ${SOCKS5_PORT}
  udp: '${SOCKS5_UDP_MODE}'
  mark: ${MARK}
EOF

  if [ -n "${SOCKS5_USERNAME}" ]; then
      echo "  username: '${SOCKS5_USERNAME}'" >> /hs5t.yml
  fi

  if [ -n "${SOCKS5_PASSWORD}" ]; then
      echo "  password: '${SOCKS5_PASSWORD}'" >> /hs5t.yml
  fi

  if [ -n "${SOCKS5_UDP_ADDR}" ]; then
      echo "  udp-address: '${SOCKS5_UDP_ADDR}'" >> /hs5t.yml
  fi
}

config_route() {
  echo "#!/bin/sh" > /route.sh
  chmod +x /route.sh

  if [ "${CONFIG_ROUTES}" == "0" ]; then
    return
  fi

  echo "ip route add default dev ${TUN} table ${TABLE}" >> /route.sh

  for addr in $(echo ${IPV4_INCLUDED_ROUTES} | tr ',' '\n'); do
    echo "ip rule add to ${addr} table ${TABLE}" >> /route.sh
  done

  echo "ip rule add to $(ip -o -f inet address show eth0 | awk '/scope global/ {print $4}') table main" >> /route.sh

  for addr in $(echo ${IPV4_EXCLUDED_ROUTES} | tr ',' '\n'); do
    echo "ip rule add to ${addr} table main" >> /route.sh
  done

  echo "ip rule add fwmark ${MARK} table main pref 1" >> /route.sh
}

run() {
  config_file
  config_route
  echo "echo 1 > /success" >> /route.sh
  hev-socks5-tunnel /hs5t.yml
}

run || exit 1
