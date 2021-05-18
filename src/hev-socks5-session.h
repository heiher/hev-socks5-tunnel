/*
 ============================================================================
 Name        : hev-socks5-session.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Session
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_H__
#define __HEV_SOCKS5_SESSION_H__

#include <hev-task.h>
#include <hev-socks5-client.h>

#include "hev-list.h"

#define HEV_SOCKS5_SESSION(p) ((HevSocks5Session *)p)
#define HEV_SOCKS5_SESSION_CLASS(p) ((HevSocks5SessionClass *)p)
#define HEV_SOCKS5_SESSION_GET_CLASS(p) ((void *)((HevSocks5Session *)p)->klass)

typedef struct _HevSocks5Session HevSocks5Session;
typedef struct _HevSocks5SessionClass HevSocks5SessionClass;

struct _HevSocks5Session
{
    HevSocks5SessionClass *klass;

    HevListNode node;
    HevSocks5Client *client;
    HevTask *task;
};

struct _HevSocks5SessionClass
{
    const char *name;

    void (*splicer) (HevSocks5Session *self);
    void (*finalizer) (HevSocks5Session *self);
};

int hev_socks5_session_construct (HevSocks5Session *self);
void hev_socks5_session_destruct (HevSocks5Session *self);

void hev_socks5_session_destroy (HevSocks5Session *self);

void hev_socks5_session_run (HevSocks5Session *self);
void hev_socks5_session_terminate (HevSocks5Session *self);

#endif /* __HEV_SOCKS5_SESSION_H__ */
