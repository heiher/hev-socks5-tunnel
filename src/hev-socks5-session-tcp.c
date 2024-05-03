/*
 ============================================================================
 Name        : hev-socks5-session-tcp.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session TCP
 ============================================================================
 */

#include <errno.h>
#include <string.h>

#include <lwip/tcp.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-misc.h>

#include "hev-utils.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-config-const.h"

#include "hev-socks5-session-tcp.h"

#define task_io_yielder hev_socks5_task_io_yielder

static int
tcp_splice_f (HevSocks5SessionTCP *self)
{
    struct iovec iov[64];
    struct pbuf *p;
    ssize_t s;
    int i;

    if (!self->queue)
        return 0;

    for (p = self->queue, i = 0; p && (i < 64); p = p->next, i++) {
        iov[i].iov_base = p->payload;
        iov[i].iov_len = p->len;
    }

    s = writev (HEV_SOCKS5 (self)->fd, iov, i);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    }

    hev_task_mutex_lock (self->mutex);
    self->queue = pbuf_free_header (self->queue, s);
    if (self->pcb)
        tcp_recved (self->pcb, s);
    hev_task_mutex_unlock (self->mutex);
    if (!self->queue)
        return 0;

    return 1;
}

static int
tcp_splice_b (HevSocks5SessionTCP *self)
{
    struct iovec iov[2];
    err_t err = ERR_OK;
    int iovlen;
    ssize_t s;

    iovlen = hev_ring_buffer_writing (self->buffer, iov);
    if (iovlen <= 0)
        return 0;

    s = readv (HEV_SOCKS5 (self)->fd, iov, iovlen);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        iovlen = hev_ring_buffer_reading (self->buffer, iov);
        if (iovlen <= 0)
            return -1;
    }
    hev_ring_buffer_write_finish (self->buffer, s);

    hev_task_mutex_lock (self->mutex);
    if (self->pcb) {
        int i;
        iovlen = hev_ring_buffer_reading (self->buffer, iov);
        for (i = 0, s = 0; i < iovlen; i++) {
            err |= tcp_write (self->pcb, iov[i].iov_base, iov[i].iov_len, 0);
            s += iov[i].iov_len;
        }
        hev_ring_buffer_read_finish (self->buffer, s);
        err |= tcp_output (self->pcb);
    }
    hev_task_mutex_unlock (self->mutex);
    if (!self->pcb || (err != ERR_OK))
        return -1;

    return 1;
}

static err_t
tcp_recv_handler (void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    HevSocks5SessionTCP *self = arg;

    if (!p) {
        hev_socks5_session_terminate (HEV_SOCKS5_SESSION (self));
        return ERR_OK;
    }

    if (!self->queue) {
        self->queue = p;
    } else {
        if (self->queue->tot_len > TCP_WND_MAX (pcb))
            return ERR_WOULDBLOCK;
        pbuf_cat (self->queue, p);
    }

    hev_task_wakeup (self->data.task);
    return ERR_OK;
}

static err_t
tcp_sent_handler (void *arg, struct tcp_pcb *pcb, u16_t len)
{
    HevSocks5SessionTCP *self = arg;

    hev_ring_buffer_read_release (self->buffer, len);
    hev_task_wakeup (self->data.task);

    return ERR_OK;
}

static void
tcp_err_handler (void *arg, err_t err)
{
    HevSocks5SessionTCP *self = arg;

    self->pcb = NULL;
    hev_socks5_session_terminate (HEV_SOCKS5_SESSION (self));
}

HevSocks5SessionTCP *
hev_socks5_session_tcp_new (struct tcp_pcb *pcb, HevTaskMutex *mutex)
{
    HevSocks5SessionTCP *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5SessionTCP));
    if (!self)
        return NULL;

    res = hev_socks5_session_tcp_construct (self, pcb, mutex);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    LOG_D ("%p socks5 session tcp new", self);

    return self;
}

static int
hev_socks5_session_tcp_bind (HevSocks5 *self, int fd,
                             const struct sockaddr *dest)
{
    HevConfigServer *srv;
    unsigned int mark;

    LOG_D ("%p socks5 session tcp bind", self);

    srv = hev_config_get_socks5_server ();
    mark = srv->mark;

    if (mark) {
        int res;

        res = set_sock_mark (fd, mark);
        if (res < 0)
            return -1;
    }

    return 0;
}

static void
hev_socks5_session_tcp_splice (HevSocks5Session *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);
    int res_f = 1;
    int res_b = 1;

    LOG_D ("%p socks5 session tcp splice", self);

    if (!self->pcb)
        return;

    self->buffer = hev_ring_buffer_alloca (tcp_sndbuf (self->pcb));
    if (!self->buffer)
        return;

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = tcp_splice_f (self);
        if (res_b >= 0)
            res_b = tcp_splice_b (self);

        if (res_f < 0 && res_b < 0)
            break;
        else if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else
            type = HEV_TASK_WAITIO;

        if (task_io_yielder (type, base) < 0)
            break;
    }

    while (self->pcb) {
        if (hev_ring_buffer_get_use_size (self->buffer) == 0)
            break;

        if (task_io_yielder (HEV_TASK_WAITIO, base) < 0)
            break;
    }
}

static HevTask *
hev_socks5_session_tcp_get_task (HevSocks5Session *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);

    return self->data.task;
}

static void
hev_socks5_session_tcp_set_task (HevSocks5Session *base, HevTask *task)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);

    self->data.task = task;
}

static HevListNode *
hev_socks5_session_tcp_get_node (HevSocks5Session *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);

    return &self->data.node;
}

int
hev_socks5_session_tcp_construct (HevSocks5SessionTCP *self,
                                  struct tcp_pcb *pcb, HevTaskMutex *mutex)
{
    struct sockaddr_in6 addr6;
    struct sockaddr *addr;
    int res;

    addr = (struct sockaddr *)&addr6;
    res = lwip_to_sock_addr (&pcb->local_ip, pcb->local_port, addr);
    if (res < 0)
        return -1;

    res = hev_socks5_client_tcp_construct_ip (&self->base, addr);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session tcp construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_TCP_TYPE;

    tcp_arg (pcb, self);
    tcp_recv (pcb, tcp_recv_handler);
    tcp_sent (pcb, tcp_sent_handler);
    tcp_err (pcb, tcp_err_handler);

    self->pcb = pcb;
    self->mutex = mutex;
    self->data.self = self;

    return 0;
}

void
hev_socks5_session_tcp_destruct (HevObject *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);

    LOG_D ("%p socks5 session tcp destruct", self);

    hev_task_mutex_lock (self->mutex);
    if (self->pcb) {
        tcp_recv (self->pcb, NULL);
        tcp_sent (self->pcb, NULL);
        tcp_err (self->pcb, NULL);
        tcp_abort (self->pcb);
    }

    if (self->queue)
        pbuf_free (self->queue);
    hev_task_mutex_unlock (self->mutex);

    HEV_SOCKS5_CLIENT_TCP_TYPE->destruct (base);
}

static void *
hev_socks5_session_tcp_iface (HevObject *base, void *type)
{
    if (type == HEV_SOCKS5_SESSION_TYPE) {
        HevSocks5SessionTCPClass *klass = HEV_OBJECT_GET_CLASS (base);
        return &klass->session;
    }

    return HEV_SOCKS5_CLIENT_TCP_TYPE->iface (base, type);
}

HevObjectClass *
hev_socks5_session_tcp_class (void)
{
    static HevSocks5SessionTCPClass klass;
    HevSocks5SessionTCPClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5Class *skptr;
        HevSocks5SessionIface *siptr;
        void *ptr;

        ptr = HEV_SOCKS5_CLIENT_TCP_TYPE;
        memcpy (kptr, ptr, sizeof (HevSocks5ClientTCPClass));

        okptr->name = "HevSocks5SessionTCP";
        okptr->destruct = hev_socks5_session_tcp_destruct;
        okptr->iface = hev_socks5_session_tcp_iface;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_tcp_bind;

        siptr = &kptr->session;
        siptr->splicer = hev_socks5_session_tcp_splice;
        siptr->get_task = hev_socks5_session_tcp_get_task;
        siptr->set_task = hev_socks5_session_tcp_set_task;
        siptr->get_node = hev_socks5_session_tcp_get_node;
    }

    return okptr;
}
