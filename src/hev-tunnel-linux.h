/*
 ============================================================================
 Name        : hev-tunnel-linux.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 - 2025 hev
 Description : Tunnel on Linux
 ============================================================================
 */

#ifndef __HEV_TUNNEL_LINUX_H__
#define __HEV_TUNNEL_LINUX_H__

struct pbuf *hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder,
                              void *yielder_data);
ssize_t hev_tunnel_write (int fd, struct pbuf *buf);

#endif /* __HEV_TUNNEL_LINUX_H__ */
