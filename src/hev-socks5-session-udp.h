/*
 ============================================================================
 Name        : hev-socks5-session-udp.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session UDP
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_UDP_H__
#define __HEV_SOCKS5_SESSION_UDP_H__

#include <hev-socks5-client-udp.h>

#include "hev-socks5-session.h"

#define HEV_SOCKS5_SESSION_UDP(p) ((HevSocks5SessionUDP *)p)
#define HEV_SOCKS5_SESSION_UDP_CLASS(p) ((HevSocks5SessionUDPClass *)p)
#define HEV_SOCKS5_SESSION_UDP_TYPE (hev_socks5_session_udp_class ())

typedef struct _HevSocks5SessionUDP HevSocks5SessionUDP;
typedef struct _HevSocks5SessionUDPClass HevSocks5SessionUDPClass;

struct _HevSocks5SessionUDP
{
    HevSocks5ClientUDP base;

    HevSocks5SessionData data;

    HevList frame_list;
    struct udp_pcb *pcb;
    HevTaskMutex *mutex;
    int frames;
    int addr;
    int port;
};

struct _HevSocks5SessionUDPClass
{
    HevSocks5ClientUDPClass base;

    HevSocks5SessionIface session;
};

HevObjectClass *hev_socks5_session_udp_class (void);

int hev_socks5_session_udp_construct (HevSocks5SessionUDP *self,
                                      struct udp_pcb *pcb, HevTaskMutex *mutex);

HevSocks5SessionUDP *hev_socks5_session_udp_new (struct udp_pcb *pcb,
                                                 HevTaskMutex *mutex);

#endif /* __HEV_SOCKS5_SESSION_UDP_H__ */
