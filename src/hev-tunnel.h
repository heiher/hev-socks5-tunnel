/*
 ============================================================================
 Name        : hev-tunnel.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel
 ============================================================================
 */

#ifndef __HEV_TUNNEL_H__
#define __HEV_TUNNEL_H__

#if defined(__linux__)
#include "hev-tunnel-linux.h"
#endif /* __linux__ */

#if defined(__FreeBSD__)
#include "hev-tunnel-freebsd.h"
#endif /* __FreeBSD__ */

#if defined(__APPLE__) || defined(__MACH__)
#include "hev-tunnel-macos.h"
#endif /* __APPLE__ || __MACH__ */

int hev_tunnel_open (const char *name, int multi_queue);
void hev_tunnel_close (int fd);

int hev_tunnel_set_mtu (int mtu);
int hev_tunnel_set_state (int state);
const char *hev_tunnel_get_name (void);

int hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix);
int hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix);

#endif /* __HEV_TUNNEL_H__ */
