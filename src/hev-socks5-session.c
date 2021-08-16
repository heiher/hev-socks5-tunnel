/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Session
 ============================================================================
 */

#include <hev-memory-allocator.h>

#include "hev-logger.h"
#include "hev-config.h"

#include "hev-socks5-session.h"

static HevSocks5SessionClass _klass = {
    .name = "HevSocks5Session",
    .finalizer = hev_socks5_session_destruct,
};

int
hev_socks5_session_construct (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session construct", self);

    HEV_SOCKS5_SESSION (self)->klass = HEV_SOCKS5_SESSION_CLASS (&_klass);

    return 0;
}

void
hev_socks5_session_destruct (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session destruct", self);

    hev_socks5_unref (HEV_SOCKS5 (self->client));
    hev_free (self);
}

void
hev_socks5_session_destroy (HevSocks5Session *self)
{
    HevSocks5SessionClass *klass = HEV_SOCKS5_SESSION_GET_CLASS (self);

    LOG_D ("%p socks5 session destroy", self);

    klass->finalizer (self);
}

void
hev_socks5_session_run (HevSocks5Session *self)
{
    HevSocks5SessionClass *klass;
    HevConfigServer *srv;
    int read_write_timeout;
    int connect_timeout;
    int res;

    LOG_D ("%p socks5 session run", self);

    srv = hev_config_get_socks5_server ();
    connect_timeout = hev_config_get_misc_connect_timeout ();
    read_write_timeout = hev_config_get_misc_read_write_timeout ();

    hev_socks5_set_timeout (HEV_SOCKS5 (self->client), connect_timeout);

    res = hev_socks5_client_connect (self->client, srv->addr, srv->port);
    if (res < 0) {
        LOG_E ("%p socks5 session connect", self);
        return;
    }

    hev_socks5_set_timeout (HEV_SOCKS5 (self->client), read_write_timeout);

    if (srv->user && srv->pass) {
        hev_socks5_set_auth_user_pass (HEV_SOCKS5 (self->client), srv->user,
                                       srv->pass);
        LOG_D ("%p socks5 client auth %s:%s", self, srv->user, srv->pass);
    }

    res = hev_socks5_client_handshake (self->client);
    if (res < 0) {
        LOG_E ("%p socks5 session handshake", self);
        return;
    }

    klass = HEV_SOCKS5_SESSION_GET_CLASS (self);
    klass->splicer (self);
}

void
hev_socks5_session_terminate (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session terminate", self);

    hev_socks5_set_timeout (HEV_SOCKS5 (self->client), 0);
    hev_task_wakeup (self->task);
}
