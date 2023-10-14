FROM alpine:latest AS builder

WORKDIR /src
COPY . /src

RUN apk add --update --no-cache make git gcc linux-headers musl-dev \
    && make

FROM alpine:latest
LABEL org.opencontainers.image.source="https://github.com/heiher/hev-socks5-tunnel"

COPY docker/entrypoint.sh /entrypoint.sh
COPY --from=builder /src/bin/hev-socks5-tunnel /usr/bin/hev-socks5-tunnel

RUN apk add --update --no-cache iproute2 \
    && chmod +x /entrypoint.sh

ENV TUN=tun0
ENV MTU=8500
ENV IPV4=198.18.0.1
ENV SOCKS5_ADDR=172.17.0.1
ENV SOCKS5_PORT=1080
ENV SOCKS5_UDP_MODE=udp

ENTRYPOINT ["/entrypoint.sh"]
