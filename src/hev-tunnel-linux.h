/*
 ============================================================================
 Name        : hev-tunnel-linux.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 everyone.
 Description : Tunnel on Linux
 ============================================================================
 */

#ifndef __HEV_TUNNEL_LINUX_H__
#define __HEV_TUNNEL_LINUX_H__

typedef struct _HevTunnelLinux HevTunnelLinux;

HevTunnelLinux *hev_tunnel_linux_new (const char *name);
void hev_tunnel_linux_destroy (HevTunnelLinux *self);

int hev_tunnel_linux_set_mtu (HevTunnelLinux *self, int mtu);

int hev_tunnel_linux_set_ipv4 (HevTunnelLinux *self, const char *addr,
                               unsigned int prefix);
int hev_tunnel_linux_set_ipv6 (HevTunnelLinux *self, const char *addr,
                               unsigned int prefix);

int hev_tunnel_linux_set_state (HevTunnelLinux *self, int state);

int hev_tunnel_linux_get_fd (HevTunnelLinux *self);

#endif /* __HEV_TUNNEL_LINUX_H__ */
