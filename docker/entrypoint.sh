#!/bin/sh

TUN="${TUN:-tun0}"
MTU="${MTU:-9000}"
IPV4="${IPV4:-198.18.0.1}"
IPV4GW=$(ip -o -f inet route show default | awk '/dev eth0/ {print $3}')
IPV6="${IPV6:-}"

MARK="${MARK:-0}"

SOCKS5_ADDR="${SOCKS5_ADDR:-172.17.0.1}"
SOCKS5_PORT="${SOCKS5_PORT:-1080}"
SOCKS5_USERNAME="${SOCKS5_USERNAME:-}"
SOCKS5_PASSWORD="${SOCKS5_PASSWORD:-}"
SOCKS5_UDP_MODE="${SOCKS5_UDP_MODE:-udp}"

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
}

config_route() {
  echo "#!/bin/sh" > /route.sh
  chmod +x /route.sh

  for addr in $(echo ${IPV4_INCLUDED_ROUTES} | tr ',' '\n'); do
    if [ ${addr} = "0.0.0.0/0" ]
    then
      echo "ip route del default" >> /route.sh
      echo "ip route add default via ${IPV4} dev ${TUN}" >> /route.sh
    else
      echo "ip route add ${addr} via ${IPV4} dev ${TUN}" >> /route.sh
    fi
  done

  echo "ip route add ${SOCKS5_ADDR} via ${IPV4GW} dev eth0" >> /route.sh

  for addr in $(echo ${IPV4_EXCLUDED_ROUTES} | tr ',' '\n'); do
    echo "ip route add ${addr} via ${IPV4GW} dev eth0" >> /route.sh
  done

  if [ ${IPV6} != "" ]
  then
    sysctl -w net.ipv6.conf.all.forwarding=1

    for v6addr in $(echo ${IPV6_INCLUDED_ROUTES} | tr ',' '\n'); do
      if [ ${v6addr} = "::/0" ]
      then
        echo "ip -6 route del default" >> /route.sh
        echo "ip -6 route add default dev ${TUN}" >> /route.sh
      else
        echo "ip route add ${v6addr} dev ${TUN}" >> /route.sh
      fi
    done

    for v6addr in $(echo ${IPV6_EXCLUDED_ROUTES} | tr ',' '\n'); do
      echo "ip route add ${v6addr} dev eth0" >> /route.sh
    done
  fi
}

run() {
  config_file
  config_route
  echo "echo 1 > /success" >> /route.sh
  hev-socks5-tunnel /hs5t.yml
}

run || exit 1