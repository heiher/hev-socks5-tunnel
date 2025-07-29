/*
 ============================================================================
 Name        : hev-tunnel-windows.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Tunnel on Windows
 ============================================================================
 */

#ifndef __HEV_TUNNEL_WINDOWS_H__
#define __HEV_TUNNEL_WINDOWS_H__

struct pbuf *hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder,
                              void *yielder_data);
ssize_t hev_tunnel_write (int fd, struct pbuf *buf);

#endif /* __HEV_TUNNEL_WINDOWS_H__ */
