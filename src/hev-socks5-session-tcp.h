/*
 ============================================================================
 Name        : hev-socks5-session-tcp.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session TCP
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_TCP_H__
#define __HEV_SOCKS5_SESSION_TCP_H__

#include <hev-ring-buffer.h>
#include <hev-socks5-client-tcp.h>

#include "hev-socks5-session.h"

#define HEV_SOCKS5_SESSION_TCP(p) ((HevSocks5SessionTCP *)p)
#define HEV_SOCKS5_SESSION_TCP_CLASS(p) ((HevSocks5SessionTCPClass *)p)
#define HEV_SOCKS5_SESSION_TCP_TYPE (hev_socks5_session_tcp_class ())

typedef struct _HevSocks5SessionTCP HevSocks5SessionTCP;
typedef struct _HevSocks5SessionTCPClass HevSocks5SessionTCPClass;

struct _HevSocks5SessionTCP
{
    HevSocks5ClientTCP base;

    HevSocks5SessionData data;

    struct pbuf *queue;
    struct tcp_pcb *pcb;
    HevTaskMutex *mutex;
    HevRingBuffer *buffer;
    int pcb_eof;
};

struct _HevSocks5SessionTCPClass
{
    HevSocks5ClientTCPClass base;

    HevSocks5SessionIface session;
};

HevObjectClass *hev_socks5_session_tcp_class (void);

int hev_socks5_session_tcp_construct (HevSocks5SessionTCP *self,
                                      struct tcp_pcb *pcb, HevTaskMutex *mutex);

HevSocks5SessionTCP *hev_socks5_session_tcp_new (struct tcp_pcb *pcb,
                                                 HevTaskMutex *mutex);

#endif /* __HEV_SOCKS5_SESSION_TCP_H__ */
