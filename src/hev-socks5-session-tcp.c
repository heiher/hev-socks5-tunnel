/*
 ============================================================================
 Name        : hev-socks5-session-tcp.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Session TCP
 ============================================================================
 */

#include <errno.h>
#include <string.h>
#include <lwip/tcp.h>
#include <netinet/in.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-misc.h>
#include <hev-socks5-client-tcp.h>

#include "hev-logger.h"
#include "hev-config-const.h"

#include "hev-socks5-session-tcp.h"

#define task_io_yielder hev_socks5_task_io_yielder

static int
tcp_splice_f (HevSocks5SessionTCP *self)
{
    ssize_t s;
    int fd;

    if (!self->queue)
        return 0;

    fd = HEV_SOCKS5 (self->base.client)->fd;

    if (self->queue->next) {
        struct pbuf *p = self->queue;
        struct iovec iov[64];
        int i;

        for (i = 0; (i < 64) && p; p = p->next) {
            iov[i].iov_base = p->payload;
            iov[i].iov_len = p->len;
            i++;
        }

        s = writev (fd, iov, i);
    } else {
        s = write (fd, self->queue->payload, self->queue->len);
    }

    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    } else {
        hev_task_mutex_lock (self->mutex);
        self->queue = pbuf_free_header (self->queue, s);
        tcp_recved (self->pcb, s);
        hev_task_mutex_unlock (self->mutex);
    }

    return 1;
}

static int
tcp_splice_b (HevSocks5SessionTCP *self, uint8_t *buffer)
{
    u8_t flags = TCP_WRITE_FLAG_COPY;
    err_t err = ERR_OK;
    size_t size;
    ssize_t s;
    int fd;

    if (!self->pcb)
        return -1;

    if (!(size = tcp_sndbuf (self->pcb))) {
        hev_task_mutex_lock (self->mutex);
        if (self->pcb)
            err = tcp_output (self->pcb);
        hev_task_mutex_unlock (self->mutex);
        if (!self->pcb || (err != ERR_OK))
            return -1;
        return 0;
    }

    if (size > TCP_BUF_SIZE)
        size = TCP_BUF_SIZE;

    fd = HEV_SOCKS5 (self->base.client)->fd;

    s = read (fd, buffer, size);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    } else if (s == size) {
        flags |= TCP_WRITE_FLAG_MORE;
    }

    hev_task_mutex_lock (self->mutex);
    if (self->pcb)
        err = tcp_write (self->pcb, buffer, s, flags);
    hev_task_mutex_unlock (self->mutex);
    if (!self->pcb || (err != ERR_OK))
        return -1;

    return 1;
}

static void
hev_socks5_session_tcp_splice (HevSocks5Session *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);
    uint8_t *buffer;
    int res_f = 1;
    int res_b = 1;

    LOG_D ("%p socks5 session tcp splice", self);

    buffer = hev_malloc (TCP_BUF_SIZE);
    if (!buffer)
        return;

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = tcp_splice_f (self);
        if (res_b >= 0)
            res_b = tcp_splice_b (self, buffer);

        if (res_f < 0 && res_b < 0)
            break;
        else if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else
            type = HEV_TASK_WAITIO;

        if (task_io_yielder (type, base->client) < 0)
            break;
    }

    hev_free (buffer);
}

static HevSocks5SessionTCPClass _klass = {
    {
        .name = "HevSoscks5SessionTCP",
        .splicer = hev_socks5_session_tcp_splice,
        .finalizer = hev_socks5_session_tcp_destruct,
    },
};

int
hev_socks5_session_tcp_construct (HevSocks5SessionTCP *self)
{
    int res;

    res = hev_socks5_session_construct (&self->base);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session tcp construct", self);

    HEV_SOCKS5_SESSION (self)->klass = HEV_SOCKS5_SESSION_CLASS (&_klass);

    return 0;
}

void
hev_socks5_session_tcp_destruct (HevSocks5Session *base)
{
    HevSocks5SessionTCP *self = HEV_SOCKS5_SESSION_TCP (base);

    LOG_D ("%p socks5 session tcp destruct", self);

    hev_task_mutex_lock (self->mutex);
    if (self->pcb) {
        tcp_recv (self->pcb, NULL);
        tcp_sent (self->pcb, NULL);
        tcp_err (self->pcb, NULL);

        if (tcp_close (self->pcb) != ERR_OK)
            tcp_abort (self->pcb);
    }

    if (self->queue)
        pbuf_free (self->queue);
    hev_task_mutex_unlock (self->mutex);

    hev_socks5_session_destruct (base);
}

static err_t
tcp_recv_handler (void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    HevSocks5SessionTCP *self = arg;
    HevSocks5Session *s = arg;

    if (!p) {
        hev_socks5_session_terminate (s);
        return ERR_OK;
    }

    if (!self->queue) {
        self->queue = p;
    } else {
        if (self->queue->tot_len > TCP_WND)
            return ERR_WOULDBLOCK;
        pbuf_cat (self->queue, p);
    }

    hev_task_wakeup (s->task);
    return ERR_OK;
}

static err_t
tcp_sent_handler (void *arg, struct tcp_pcb *pcb, u16_t len)
{
    HevSocks5Session *s = arg;

    hev_task_wakeup (s->task);

    return ERR_OK;
}

static void
tcp_err_handler (void *arg, err_t err)
{
    HevSocks5SessionTCP *self = arg;
    HevSocks5Session *s = arg;

    self->pcb = NULL;
    hev_socks5_session_terminate (s);
}

HevSocks5SessionTCP *
hev_socks5_session_tcp_new (struct tcp_pcb *pcb, HevTaskMutex *mutex)
{
    HevSocks5SessionTCP *self;
    HevSocks5ClientTCP *tcp;
    struct sockaddr_in6 ad6;
    struct sockaddr_in ad4;
    struct sockaddr *addr;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5SessionTCP));
    if (!self)
        return NULL;

    LOG_D ("%p socks5 session tcp new", self);

    switch (pcb->local_ip.type) {
    case IPADDR_TYPE_V4:
        ad4.sin_family = AF_INET;
        ad4.sin_port = htons (pcb->local_port);
        memcpy (&ad4.sin_addr, &pcb->local_ip, 4);
        addr = (struct sockaddr *)&ad4;
        break;
    case IPADDR_TYPE_V6:
        ad6.sin6_family = AF_INET6;
        ad6.sin6_port = htons (pcb->local_port);
        memcpy (&ad6.sin6_addr, &pcb->local_ip, 16);
        addr = (struct sockaddr *)&ad6;
        break;
    default:
        hev_free (self);
        return NULL;
    }

    res = hev_socks5_session_tcp_construct (self);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    tcp = hev_socks5_client_tcp_new_ip (addr);
    if (!tcp) {
        hev_free (self);
        return NULL;
    }

    tcp_arg (pcb, self);
    tcp_recv (pcb, tcp_recv_handler);
    tcp_sent (pcb, tcp_sent_handler);
    tcp_err (pcb, tcp_err_handler);

    self->pcb = pcb;
    self->mutex = mutex;
    self->base.client = HEV_SOCKS5_CLIENT (tcp);

    return self;
}
