/*
 ============================================================================
 Name        : hev-wintun.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Wintun utils
 ============================================================================
 */

#ifndef __HEV_WINTUN_H__
#define __HEV_WINTUN_H__

#define HEV_WINTUN_EAGAIN (259)

typedef void HevWinTun;
typedef void HevWinTunAdapter;
typedef void HevWinTunSession;

HevWinTun *hev_wintun_open (void);
void hev_wintun_close (HevWinTun *self);

HevWinTunAdapter *hev_wintun_adapter_create (const char *name);
void hev_wintun_adapter_close (HevWinTunAdapter *adapter);

int hev_wintun_adapter_get_index (HevWinTunAdapter *adapter);
int hev_wintun_adapter_set_mtu (HevWinTunAdapter *adapter, int mtu);
int hev_wintun_adapter_set_ipv4 (HevWinTunAdapter *adapter, char addr[4],
                                 unsigned int prefix);
int hev_wintun_adapter_set_ipv6 (HevWinTunAdapter *adapter, char addr[16],
                                 unsigned int prefix);

HevWinTunSession *hev_wintun_session_start (HevWinTunAdapter *adapter);
void hev_wintun_session_stop (HevWinTunSession *session);

void *hev_wintun_session_get_read_wait_event (HevWinTunSession *session);

void *hev_wintun_session_receive (HevWinTunSession *session, int *size);
void hev_wintun_session_release (HevWinTunSession *session, void *packet);

void *hev_wintun_session_allocate (HevWinTunSession *session, int size);
void hev_wintun_session_send (HevWinTunSession *session, void *packet);

unsigned int hev_wintun_get_last_error (void);

#endif /* __HEV_WINTUN_H__ */
