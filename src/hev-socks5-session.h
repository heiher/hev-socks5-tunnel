/*
 ============================================================================
 Name        : hev-socks5-session.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2020 Everyone.
 Description : Socks5 Session
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_H__
#define __HEV_SOCKS5_SESSION_H__

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>

#include <hev-task.h>

typedef struct _HevSocks5SessionBase HevSocks5SessionBase;
typedef struct _HevSocks5Session HevSocks5Session;
typedef void (*HevSocks5SessionCloseNotify) (HevSocks5Session *self);

struct _HevSocks5SessionBase
{
    HevSocks5SessionBase *prev;
    HevSocks5SessionBase *next;
    HevTask *task;
    int hp;
};

HevSocks5Session *
hev_socks5_session_new_tcp (struct tcp_pcb *pcb, HevTaskMutex *mutex,
                            HevSocks5SessionCloseNotify notify);
HevSocks5Session *
hev_socks5_session_new_udp (struct udp_pcb *pcb, HevTaskMutex *mutex,
                            HevSocks5SessionCloseNotify notify);

HevSocks5Session *hev_socks5_session_ref (HevSocks5Session *self);
void hev_socks5_session_unref (HevSocks5Session *self);

void hev_socks5_session_run (HevSocks5Session *self);

#endif /* __HEV_SOCKS5_SESSION_H__ */
