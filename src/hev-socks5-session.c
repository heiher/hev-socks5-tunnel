/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session
 ============================================================================
 */

#include <string.h>

#include "hev-utils.h"
#include "hev-logger.h"
#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-socks5-client.h"
#include "hev-socks5-tunnel.h"

#include "hev-socks5-session.h"

void
hev_socks5_session_run (HevSocks5Session *self)
{
    HevSocks5SessionIface *iface;
    HevConfigServer *srv;
    int res;

    LOG_D ("%p socks5 session run", self);

    srv = hev_config_get_socks5_server ();

    res = hev_socks5_client_connect (HEV_SOCKS5_CLIENT (self), srv->addr,
                                     srv->port);
    if (res < 0) {
        LOG_I ("%p socks5 session connect", self);
        return;
    }

    if (srv->user && srv->pass) {
        hev_socks5_client_set_auth (HEV_SOCKS5_CLIENT (self), srv->user,
                                    srv->pass);
        LOG_D ("%p socks5 client auth %s:%s", self, srv->user, srv->pass);
    }

    res = hev_socks5_client_handshake (HEV_SOCKS5_CLIENT (self), srv->pipeline);
    if (res < 0) {
        LOG_I ("%p socks5 session handshake", self);
        return;
    }

    iface = HEV_OBJECT_GET_IFACE (self, HEV_SOCKS5_SESSION_TYPE);
    iface->splicer (self);
}

void
hev_socks5_session_terminate (HevSocks5Session *self)
{
    HevSocks5SessionIface *iface;

    LOG_D ("%p socks5 session terminate", self);

    iface = HEV_OBJECT_GET_IFACE (self, HEV_SOCKS5_SESSION_TYPE);
    hev_socks5_set_timeout (HEV_SOCKS5 (self), 0);
    hev_task_wakeup (iface->get_task (self));
}

/*
 * Wait for I/O while the session has nothing in flight. The caller must have
 * left the frame that holds its buffers, so that after an idle period the
 * stack memory below can be released. A timeout not longer than the idle
 * period is waited as is, without releasing.
 *
 * Returns 1 when the stack has been released. The caller must poll its I/O
 * again, as a wakeup arriving with the idle timer is not seen here, and pass
 * released as nonzero to the next wait if it still has nothing to do.
 */
int
hev_socks5_session_idle_wait (HevSocks5Session *self, int released)
{
    HevSocks5 *s5 = HEV_SOCKS5 (self);
    int timeout = s5->timeout;
    int remain;

    if (!released && (timeout < 0 || timeout > SESSION_IDLE_RELEASE_TIME)) {
        remain = hev_task_sleep (SESSION_IDLE_RELEASE_TIME);
        if (remain > 0)
            goto woken;

        hev_task_release_stack (hev_task_self ());
        return 1;
    }

    if (timeout < 0) {
        hev_task_yield (HEV_TASK_WAITIO);
        goto woken;
    }

    /* The timeout may be changed (terminate) while sleeping. */
    if (released && timeout > SESSION_IDLE_RELEASE_TIME)
        timeout -= SESSION_IDLE_RELEASE_TIME;

    remain = hev_task_sleep (timeout);
    if (remain <= 0) {
        LOG_I ("%p io timeout", self);
        return -1;
    }

woken:
    hev_socks5_tunnel_update_session (hev_socks5_session_get_node (self));
    return 0;
}

void
hev_socks5_session_set_task (HevSocks5Session *self, HevTask *task)
{
    HevSocks5SessionIface *iface;

    iface = HEV_OBJECT_GET_IFACE (self, HEV_SOCKS5_SESSION_TYPE);
    iface->set_task (self, task);
}

HevListNode *
hev_socks5_session_get_node (HevSocks5Session *self)
{
    HevSocks5SessionIface *iface;

    iface = HEV_OBJECT_GET_IFACE (self, HEV_SOCKS5_SESSION_TYPE);
    return iface->get_node (self);
}

int
hev_socks5_session_bind (HevSocks5 *self, int fd, const struct sockaddr *dest)
{
    HevConfigServer *srv;
    unsigned int mark;

    LOG_D ("%p socks5 session bind", self);

    srv = hev_config_get_socks5_server ();
    mark = srv->mark;

    if (mark) {
        int res;

        res = set_sock_mark (fd, mark);
        if (res < 0)
            return -1;
    }

    set_sock_tcp_fastopen (fd, srv->fastopen);

    return 0;
}

void *
hev_socks5_session_iface (void)
{
    static char type;

    return &type;
}
