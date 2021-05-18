/*
 ============================================================================
 Name        : hev-socks5-tunnel.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2021 hev
 Description : Socks5 Tunnel
 ============================================================================
 */

#ifndef __HEV_SOCKS5_TUNNEL_H__
#define __HEV_SOCKS5_TUNNEL_H__

int hev_socks5_tunnel_init (int tunfd);
void hev_socks5_tunnel_fini (void);

int hev_socks5_tunnel_run (void);
void hev_socks5_tunnel_stop (void);

#endif /* __HEV_SOCKS5_TUNNEL_H__ */
