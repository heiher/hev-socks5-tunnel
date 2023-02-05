/*
 ============================================================================
 Name        : hev-socks5-session.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_H__
#define __HEV_SOCKS5_SESSION_H__

#include <hev-task.h>

#include "hev-list.h"

#define HEV_SOCKS5_SESSION(p) ((HevSocks5Session *)p)
#define HEV_SOCKS5_SESSION_IFACE(p) ((HevSocks5SessionIface *)p)
#define HEV_SOCKS5_SESSION_TYPE (hev_socks5_session_iface ())

typedef void HevSocks5Session;
typedef struct _HevSocks5SessionData HevSocks5SessionData;
typedef struct _HevSocks5SessionIface HevSocks5SessionIface;

struct _HevSocks5SessionData
{
    HevListNode node;
    HevTask *task;
    HevSocks5Session *self;
};

struct _HevSocks5SessionIface
{
    void (*splicer) (HevSocks5Session *self);
    HevTask *(*get_task) (HevSocks5Session *self);
    void (*set_task) (HevSocks5Session *self, HevTask *task);
    HevListNode *(*get_node) (HevSocks5Session *self);
};

void *hev_socks5_session_iface (void);

void hev_socks5_session_run (HevSocks5Session *self);
void hev_socks5_session_terminate (HevSocks5Session *self);

void hev_socks5_session_set_task (HevSocks5Session *self, HevTask *task);
HevListNode *hev_socks5_session_get_node (HevSocks5Session *self);

#endif /* __HEV_SOCKS5_SESSION_H__ */
