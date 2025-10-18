/*
 ============================================================================
 Name        : hev-socks5-session-udp.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2023 hev
 Description : Socks5 Session UDP
 ============================================================================
 */

#include <errno.h>
#include <string.h>

#include <lwip/udp.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-udp.h>
#include <hev-socks5-misc.h>

#include "hev-utils.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-compiler.h"
#include "hev-config-const.h"
#include "hev-socks5-tunnel.h"

#include "hev-socks5-session-udp.h"

typedef struct _HevSocks5UDPFrame HevSocks5UDPFrame;

struct _HevSocks5UDPFrame
{
    HevListNode node;
    HevSocks5Addr addr;
    struct pbuf *data;
};

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5 *self = data;
    HevListNode *node;
    int res;

    if (self->type == HEV_SOCKS5_TYPE_UDP_IN_UDP) {
        ssize_t res;
        char buf;

        res = recv (self->fd, &buf, sizeof (buf), 0);
        if ((res == 0) || ((res < 0) && (errno != EAGAIN))) {
            hev_socks5_set_timeout (self, 0);
            return -1;
        }
    }

    res = hev_socks5_task_io_yielder (type, data);
    node = hev_socks5_session_get_node (HEV_SOCKS5_SESSION (self));
    hev_socks5_tunnel_update_session (node);

    return res;
}

static int
hev_socks5_session_udp_fwd_f (HevSocks5SessionUDP *self, unsigned int num)
{
    HevSocks5UDPMsg msgv[num];
    HevSocks5UDPFrame *frame;
    HevListNode *node;
    struct pbuf *buf;
    int i, res;

    res = self->frames;
    if (res <= 0)
        return 0;

    res = (res > num) ? num : res;
    node = hev_list_first (&self->frame_list);
    for (i = 0; i < res; i++) {
        frame = container_of (node, HevSocks5UDPFrame, node);
        node = hev_list_node_next (node);
        buf = frame->data;

        msgv[i].buf = buf->payload;
        msgv[i].len = buf->len;
        msgv[i].addr = &frame->addr;
    }

    res = hev_socks5_udp_sendmmsg (HEV_SOCKS5_UDP (self), msgv, res);
    if (res <= 0) {
        LOG_D ("%p socks5 session udp fwd f send", self);
        return -1;
    }

    for (i = 0; i < res; i++) {
        node = hev_list_first (&self->frame_list);
        frame = container_of (node, HevSocks5UDPFrame, node);
        buf = frame->data;

        hev_list_del (&self->frame_list, node);
        hev_free (frame);
        pbuf_free (buf);
        self->frames--;
    }

    return 1;
}

static int
hev_socks5_session_udp_fwd_b (HevSocks5SessionUDP *self, unsigned int num)
{
    char buf[UDP_BUF_SIZE * num];
    HevSocks5UDPMsg msgv[num];
    int i, res;

    for (i = 0; i < num; i++) {
        msgv[i].buf = buf + UDP_BUF_SIZE * i;
        msgv[i].len = UDP_BUF_SIZE;
    }

    res = hev_socks5_udp_recvmmsg (HEV_SOCKS5_UDP (self), msgv, num, 1);
    if (res <= 0) {
        if (res == -1 && errno == EAGAIN)
            return 0;
        LOG_D ("%p socks5 session udp fwd b recv", self);
        return -1;
    }

    for (i = 0; i < res; i++) {
        ip_addr_t saddr;
        struct pbuf *b;
        uint16_t port;
        err_t err;
        int ret;

        if (self->addr && self->port) {
            ip_2_ip4 (&saddr)->addr = self->addr;
            port = self->port;
        } else {
            ret = hev_socks5_addr_into_lwip (msgv[i].addr, &saddr, &port);
            if (ret < 0) {
                LOG_D ("%p socks5 session udp fwd b addr", self);
                return -1;
            }
        }

        b = pbuf_alloc_reference (msgv[i].buf, msgv[i].len, PBUF_REF);
        if (!b) {
            LOG_D ("%p socks5 session udp fwd b buf", self);
            return -1;
        }

        hev_task_mutex_lock (self->mutex);
        err = udp_sendfrom (self->pcb, b, &saddr, port);
        hev_task_mutex_unlock (self->mutex);

        pbuf_free (b);
        if (err != ERR_OK) {
            LOG_D ("%p socks5 session udp fwd b send", self);
            return -1;
        }
    }

    return 1;
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5SessionUDP *self = arg;
    HevSocks5UDPFrame *frame;

    if (!p) {
        hev_socks5_session_terminate (HEV_SOCKS5_SESSION (self));
        return;
    }

    if (self->frames > UDP_POOL_SIZE) {
        pbuf_free (p);
        return;
    }

    frame = hev_malloc (sizeof (HevSocks5UDPFrame));
    if (!frame) {
        pbuf_free (p);
        return;
    }

    frame->data = p;
    memset (&frame->node, 0, sizeof (frame->node));
    hev_socks5_addr_from_lwip (&frame->addr, &pcb->local_ip, pcb->local_port);

    if (frame->addr.atype == HEV_SOCKS5_ADDR_TYPE_NAME) {
        self->addr = ip_2_ip4 (&pcb->local_ip)->addr;
        self->port = pcb->local_port;
    }

    self->frames++;
    hev_list_add_tail (&self->frame_list, &frame->node);
    hev_task_wakeup (self->data.task);
}

HevSocks5SessionUDP *
hev_socks5_session_udp_new (struct udp_pcb *pcb, HevTaskMutex *mutex)
{
    HevSocks5SessionUDP *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5SessionUDP));
    if (!self)
        return NULL;

    res = hev_socks5_session_udp_construct (self, pcb, mutex);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    LOG_D ("%p socks5 session udp new", self);

    return self;
}

static int
hev_socks5_session_udp_bind (HevSocks5 *self, int fd,
                             const struct sockaddr *dest)
{
    HevConfigServer *srv;
    unsigned int mark;

    LOG_D ("%p socks5 session udp bind", self);

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

static uint16_t
hev_socks5_addr_get_port (const HevSocks5Addr *addr)
{
    uint16_t port = 0;

    switch (addr->atype) {
    case HEV_SOCKS5_ADDR_TYPE_IPV4:
        port = addr->ipv4.port;
        break;
    case HEV_SOCKS5_ADDR_TYPE_IPV6:
        port = addr->ipv6.port;
        break;
    case HEV_SOCKS5_ADDR_TYPE_NAME:
        memcpy (&port, addr->domain.addr + addr->domain.len, 2);
    }

    return port;
}

static int
hev_socks5_session_udp_set_upstream_addr (HevSocks5Client *base,
                                          HevSocks5Addr *addr)
{
    HevConfigServer *srv = hev_config_get_socks5_server ();
    HevSocks5ClientClass *ckptr;

    if (srv->udp_in_udp && srv->udp_addr[0]) {
        uint16_t port = hev_socks5_addr_get_port (addr);
        hev_socks5_addr_from_name (addr, srv->udp_addr, port);
    }

    ckptr = HEV_SOCKS5_CLIENT_CLASS (HEV_SOCKS5_CLIENT_UDP_TYPE);
    return ckptr->set_upstream_addr (base, addr);
}

static void
hev_socks5_session_udp_splice (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);
    HevTask *task = hev_task_self ();
    int res_f = 1, res_b = 1;
    int num;
    int fd;

    LOG_D ("%p socks5 session udp splice", self);

    num = hev_config_get_misc_udp_copy_buffer_nums ();
    fd = hev_socks5_udp_get_fd (HEV_SOCKS5_UDP (self));
    if (hev_task_mod_fd (task, fd, POLLIN | POLLOUT) < 0)
        hev_task_add_fd (task, fd, POLLIN | POLLOUT);

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = hev_socks5_session_udp_fwd_f (self, num);
        if (res_b >= 0)
            res_b = hev_socks5_session_udp_fwd_b (self, num);

        if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else if ((res_f & res_b) == 0)
            type = HEV_TASK_WAITIO;
        else
            break;

        if (task_io_yielder (type, self))
            break;
    }
}

static HevTask *
hev_socks5_session_udp_get_task (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    return self->data.task;
}

static void
hev_socks5_session_udp_set_task (HevSocks5Session *base, HevTask *task)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    self->data.task = task;
}

static HevListNode *
hev_socks5_session_udp_get_node (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);

    return &self->data.node;
}

int
hev_socks5_session_udp_construct (HevSocks5SessionUDP *self,
                                  struct udp_pcb *pcb, HevTaskMutex *mutex)
{
    HevConfigServer *srv = hev_config_get_socks5_server ();
    int type;
    int res;

    if (srv->udp_in_udp)
        type = HEV_SOCKS5_TYPE_UDP_IN_UDP;
    else
        type = HEV_SOCKS5_TYPE_UDP_IN_TCP;

    res = hev_socks5_client_udp_construct (&self->base, type);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session udp construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_UDP_TYPE;

    udp_recv (pcb, udp_recv_handler, self);

    self->pcb = pcb;
    self->mutex = mutex;
    self->data.self = self;

    return 0;
}

void
hev_socks5_session_udp_destruct (HevObject *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);
    HevListNode *node;

    LOG_D ("%p socks5 session udp destruct", self);

    node = hev_list_first (&self->frame_list);
    while (node) {
        HevSocks5UDPFrame *frame;

        frame = container_of (node, HevSocks5UDPFrame, node);
        node = hev_list_node_next (node);
        pbuf_free (frame->data);
        hev_free (frame);
    }

    hev_task_mutex_lock (self->mutex);
    if (self->pcb) {
        udp_recv (self->pcb, NULL, NULL);
        udp_remove (self->pcb);
    }
    hev_task_mutex_unlock (self->mutex);

    HEV_SOCKS5_CLIENT_UDP_TYPE->destruct (base);
}

static void *
hev_socks5_session_udp_iface (HevObject *base, void *type)
{
    if (type == HEV_SOCKS5_SESSION_TYPE) {
        HevSocks5SessionUDPClass *klass = HEV_OBJECT_GET_CLASS (base);
        return &klass->session;
    }

    return HEV_SOCKS5_CLIENT_UDP_TYPE->iface (base, type);
}

HevObjectClass *
hev_socks5_session_udp_class (void)
{
    static HevSocks5SessionUDPClass klass;
    HevSocks5SessionUDPClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5Class *skptr;
        HevSocks5ClientClass *ckptr;
        HevSocks5SessionIface *siptr;
        void *ptr;

        ptr = HEV_SOCKS5_CLIENT_UDP_TYPE;
        memcpy (kptr, ptr, sizeof (HevSocks5ClientUDPClass));

        okptr->name = "HevSocks5SessionUDP";
        okptr->destruct = hev_socks5_session_udp_destruct;
        okptr->iface = hev_socks5_session_udp_iface;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_udp_bind;

        ckptr = HEV_SOCKS5_CLIENT_CLASS (kptr);
        ckptr->set_upstream_addr = hev_socks5_session_udp_set_upstream_addr;

        siptr = &kptr->session;
        siptr->splicer = hev_socks5_session_udp_splice;
        siptr->get_task = hev_socks5_session_udp_get_task;
        siptr->set_task = hev_socks5_session_udp_set_task;
        siptr->get_node = hev_socks5_session_udp_get_node;
    }

    return okptr;
}
