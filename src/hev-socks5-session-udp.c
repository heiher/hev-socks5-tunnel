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

    if (self->type == HEV_SOCKS5_TYPE_UDP_IN_UDP) {
        ssize_t res;
        char buf;

        res = recv (self->fd, &buf, sizeof (buf), 0);
        if ((res == 0) || ((res < 0) && (errno != EAGAIN))) {
            hev_socks5_set_timeout (self, 0);
            return -1;
        }
    }

    return hev_socks5_task_io_yielder (type, data);
}

static int
hev_socks5_session_udp_fwd_f (HevSocks5SessionUDP *self)
{
    HevSocks5UDPFrame *frame;
    HevListNode *node;
    HevSocks5UDP *udp;
    struct pbuf *buf;
    int res;

    for (;;) {
        node = hev_list_first (&self->frame_list);
        if (node)
            break;

        res = task_io_yielder (HEV_TASK_WAITIO, self);
        if (res < 0) {
            self->alive &= ~HEV_SOCKS5_SESSION_UDP_ALIVE_F;
            if (self->alive && hev_socks5_get_timeout (HEV_SOCKS5 (self)))
                return 0;
            return -1;
        }
    }

    frame = container_of (node, HevSocks5UDPFrame, node);
    buf = frame->data;

    udp = HEV_SOCKS5_UDP (self);
    res = hev_socks5_udp_sendto (udp, buf->payload, buf->len, &frame->addr);
    hev_list_del (&self->frame_list, node);
    hev_free (frame);
    pbuf_free (buf);
    self->frames--;
    if (res <= 0) {
        if (res < -1) {
            self->alive &= ~HEV_SOCKS5_SESSION_UDP_ALIVE_F;
            if (self->alive && hev_socks5_get_timeout (HEV_SOCKS5 (self)))
                return 0;
        }
        if (HEV_SOCKS5 (self)->type == HEV_SOCKS5_TYPE_UDP_IN_TCP)
            hev_socks5_set_timeout (HEV_SOCKS5 (self), 0);
        LOG_D ("%p socks5 session udp fwd f send", self);
        res = -1;
    }

    self->alive |= HEV_SOCKS5_SESSION_UDP_ALIVE_F;

    return 0;
}

static int
hev_socks5_session_udp_fwd_b (HevSocks5SessionUDP *self)
{
    HevSocks5UDP *udp = HEV_SOCKS5_UDP (self);
    HevSocks5Addr addr;
    err_t err = ERR_OK;
    struct pbuf *buf;
    ip_addr_t saddr;
    uint16_t port;
    int res;

    buf = pbuf_alloc (PBUF_TRANSPORT, UDP_BUF_SIZE, PBUF_RAM);
    if (!buf) {
        LOG_D ("%p socks5 session udp fwd b buf", self);
        return -1;
    }

    res = hev_socks5_udp_recvfrom (udp, buf->payload, buf->len, &addr);
    if (res <= 0) {
        if (res < -1) {
            self->alive &= ~HEV_SOCKS5_SESSION_UDP_ALIVE_B;
            if (self->alive && hev_socks5_get_timeout (HEV_SOCKS5 (self))) {
                pbuf_free (buf);
                return 0;
            }
        }
        if (HEV_SOCKS5 (self)->type == HEV_SOCKS5_TYPE_UDP_IN_TCP)
            hev_socks5_set_timeout (HEV_SOCKS5 (self), 0);
        LOG_D ("%p socks5 session udp fwd b recv", self);
        pbuf_free (buf);
        return -1;
    }

    buf->len = res;
    buf->tot_len = res;

    if (self->addr && self->port) {
        ip_2_ip4 (&saddr)->addr = self->addr;
        port = self->port;
    } else {
        res = hev_socks5_addr_into_lwip (&addr, &saddr, &port);
        if (res < 0) {
            LOG_D ("%p socks5 session udp fwd b addr", self);
            pbuf_free (buf);
            return -1;
        }
    }

    hev_task_mutex_lock (self->mutex);
    err = udp_sendfrom (self->pcb, buf, &saddr, port);
    hev_task_mutex_unlock (self->mutex);
    pbuf_free (buf);

    if (err != ERR_OK) {
        LOG_D ("%p socks5 session udp fwd b send", self);
        return -1;
    }

    self->alive |= HEV_SOCKS5_SESSION_UDP_ALIVE_B;

    return 0;
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

static void
splice_task_entry (void *data)
{
    HevTask *task = hev_task_self ();
    HevSocks5SessionUDP *self = data;
    int fd;

    fd = hev_task_io_dup (hev_socks5_udp_get_fd (HEV_SOCKS5_UDP (self)));
    if (fd < 0)
        return;

    if (hev_task_add_fd (task, fd, POLLIN) < 0)
        hev_task_mod_fd (task, fd, POLLIN);

    for (;;) {
        if (hev_socks5_session_udp_fwd_b (self) < 0)
            break;
    }

    self->alive &= ~HEV_SOCKS5_SESSION_UDP_ALIVE_B;
    hev_task_del_fd (task, fd);
    close (fd);
}

static void
hev_socks5_session_udp_splice (HevSocks5Session *base)
{
    HevSocks5SessionUDP *self = HEV_SOCKS5_SESSION_UDP (base);
    HevTask *task = hev_task_self ();
    int stack_size;
    int fd;

    LOG_D ("%p socks5 session udp splice", self);

    self->alive = HEV_SOCKS5_SESSION_UDP_ALIVE_F |
                  HEV_SOCKS5_SESSION_UDP_ALIVE_B;
    fd = hev_socks5_udp_get_fd (HEV_SOCKS5_UDP (self));
    if (hev_task_mod_fd (task, fd, POLLOUT) < 0)
        hev_task_add_fd (task, fd, POLLOUT);

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    hev_task_ref (task);
    hev_task_run (task, splice_task_entry, self);

    for (;;) {
        if (hev_socks5_session_udp_fwd_f (self) < 0)
            break;
    }

    self->alive &= ~HEV_SOCKS5_SESSION_UDP_ALIVE_F;
    hev_task_join (task);
    hev_task_unref (task);
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
        HevSocks5SessionIface *siptr;
        void *ptr;

        ptr = HEV_SOCKS5_CLIENT_UDP_TYPE;
        memcpy (kptr, ptr, sizeof (HevSocks5ClientUDPClass));

        okptr->name = "HevSocks5SessionUDP";
        okptr->destruct = hev_socks5_session_udp_destruct;
        okptr->iface = hev_socks5_session_udp_iface;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_udp_bind;

        siptr = &kptr->session;
        siptr->splicer = hev_socks5_session_udp_splice;
        siptr->get_task = hev_socks5_session_udp_get_task;
        siptr->set_task = hev_socks5_session_udp_set_task;
        siptr->get_node = hev_socks5_session_udp_get_node;
    }

    return okptr;
}
