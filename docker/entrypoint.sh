#!/bin/sh

TUN="${TUN:-tun0}"
MTU="${MTU:-8500}"
IPV4="${IPV4:-198.18.0.1}"
IPV6="${IPV6:-}"
ICMP="${ICMP:-off}"

CONFIG_ROUTES="${CONFIG_ROUTES:-1}"

TABLE="${TABLE:-20}"
if [ "${CONFIG_ROUTES}" = "0" ]; then
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

DNS_CACHE="${DNS_CACHE:-0}"
case "$(echo ${DNS_CACHE} | tr '[:upper:]' '[:lower:]')" in
  ''|0|no|off|false) DNS_CACHE=0 ;;
  *) DNS_CACHE=1 ;;
esac
DNS_CACHE_SERVERS="${DNS_CACHE_SERVERS:-8.8.8.8,1.1.1.1}"
DNS_CACHE_SIZE="${DNS_CACHE_SIZE:-10000}"

config_file() {
  cat > /hs5t.yml << EOF
misc:
  log-level: '${LOG_LEVEL}'
tunnel:
  name: '${TUN}'
  mtu: ${MTU}
  ipv4: '${IPV4}'
  ipv6: '${IPV6}'
  icmp: '${ICMP}'
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

  if [ "${CONFIG_ROUTES}" = "0" ]; then
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

config_dns_cache() {
  if [ "${DNS_CACHE}" != "1" ]; then
    return
  fi

  cat > /dnsmasq.conf << EOF
no-resolv
no-poll
no-hosts
bind-dynamic
cache-size=${DNS_CACHE_SIZE}
log-facility=-
EOF
  echo "listen-address=127.0.0.1" >> /dnsmasq.conf
  echo "listen-address=${IPV4}" >> /dnsmasq.conf

  if [ -n "${IPV6}" ]; then
    echo "listen-address=${IPV6}" >> /dnsmasq.conf
  fi

  # upstream servers are reached via the tun device (remote resolution)
  for addr in $(echo ${DNS_CACHE_SERVERS} | tr ',' ' '); do
    echo "server=${addr}" >> /dnsmasq.conf
  done

  if [ "${LOG_LEVEL}" = "debug" ]; then
    echo "log-queries" >> /dnsmasq.conf
  fi
}

run_dns_cache() {
  if [ "${DNS_CACHE}" != "1" ]; then
    return
  fi

  if ! dnsmasq --test --conf-file=/dnsmasq.conf > /dev/null 2>&1; then
    echo "Error: invalid dnsmasq config." >&2
    return 1
  fi

  dnsmasq --conf-file=/dnsmasq.conf --keep-in-foreground 2>&1 &
  DNS_CACHE_PID=$!
}

stop_dns_cache() {
  if [ -n "${DNS_CACHE_PID}" ]; then
    kill -TERM "${DNS_CACHE_PID}" 2> /dev/null
  fi
}

on_signal() {
  [ -n "${TUNNEL_PID}" ] && kill -TERM "${TUNNEL_PID}" 2> /dev/null
}

run() {
  config_file
  config_route
  config_dns_cache || return 1
  run_dns_cache || return 1
  echo "echo 1 > /success" >> /route.sh

  hev-socks5-tunnel /hs5t.yml &
  TUNNEL_PID=$!
  trap on_signal TERM INT
  status=0
  while :; do
    wait ${TUNNEL_PID} || status=$?
    kill -0 ${TUNNEL_PID} 2> /dev/null || break
  done
  trap - TERM INT
  TUNNEL_PID=

  stop_dns_cache
  return ${status}
}

run || exit 1
