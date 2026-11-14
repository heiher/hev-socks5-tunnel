/*
 ============================================================================
 Name        : hev-socks5-tunnel.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Socks5 Tunnel
 ============================================================================
 */

#ifndef __HEV_SOCKS5_TUNNEL_H__
#define __HEV_SOCKS5_TUNNEL_H__

#include "hev-list.h"

typedef struct _HevSocks5SessionData HevSocks5SessionData;

int hev_socks5_tunnel_init (int tun_fd);
void hev_socks5_tunnel_fini (void);

int hev_socks5_tunnel_run (void);
void hev_socks5_tunnel_stop (void);

void hev_socks5_tunnel_stats (size_t *tx_packets, size_t *tx_bytes,
                              size_t *rx_packets, size_t *rx_bytes);

void hev_socks5_tunnel_update_session (HevListNode *node);
void hev_socks5_tunnel_session_replied (HevSocks5SessionData *sd);

/* misc.max-buffer-size, 0: unlimited, the buffers are not accounted then. */
extern size_t hev_socks5_tunnel_buffer_limit;

void hev_socks5_tunnel_buffer_charge (HevSocks5SessionData *sd, int size);
int hev_socks5_tunnel_buffer_admit (int size);
unsigned int hev_socks5_tunnel_window_target (unsigned int window,
                                              unsigned int floor,
                                              unsigned int max);
void hev_socks5_tunnel_window_set (unsigned int *window, unsigned int size);

#endif /* __HEV_SOCKS5_TUNNEL_H__ */
