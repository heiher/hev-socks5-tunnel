/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2021 Everyone.
 Description : Socks5 Session
 ============================================================================
 */

#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <hev-task.h>
#include <hev-task-mutex.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-memory-allocator.h>

#include "hev-config.h"
#include "hev-logger.h"

#include "hev-socks5-session.h"

#define UDP_SESSION_HP (2)
#define DEF_SESSION_HP (10)
#define SADDR_SIZE (64)
#define BUFFER_SIZE (8192)
#define TASK_STACK_SIZE (20480)

typedef struct _Socks5AuthHeader Socks5AuthHeader;
typedef struct _Socks5ReqResHeader Socks5ReqResHeader;
typedef struct _Socks5UDPHeader Socks5UDPHeader;

enum
{
    STEP_NULL,
    STEP_DO_CONNECT,
    STEP_WRITE_REQUEST,
    STEP_READ_RESPONSE,
    STEP_DO_SPLICE,
    STEP_DO_FWD_UDP,
    STEP_DO_FWD_DNS,
    STEP_CLOSE_SESSION,
};

enum
{
    TYPE_TCP,
    TYPE_UDP,
    TYPE_DNS,
};

struct _HevSocks5Session
{
    HevSocks5SessionBase base;

    int type;
    int remote_fd;
    int ref_count;

    union
    {
        struct tcp_pcb *tcp;
        struct udp_pcb *udp;
    };

    union
    {
        struct pbuf *query;
        struct pbuf *queue;
    };

    struct
    {
        ip_addr_t addr;
        u16_t port;
    };

    char *saddr;
    HevTaskMutex *mutex;
    HevSocks5SessionCloseNotify notify;
};

struct _Socks5AuthHeader
{
    uint8_t ver;
    union
    {
        uint8_t method;
        uint8_t method_len;
    };
    uint8_t methods[256];
} __attribute__ ((packed));

struct _Socks5ReqResHeader
{
    uint8_t ver;
    union
    {
        uint8_t cmd;
        uint8_t rep;
    };
    uint8_t rsv;
    uint8_t atype;
    union
    {
        struct
        {
            uint32_t addr;
            uint16_t port;
        } ipv4;
        struct
        {
            uint8_t addr[16];
            uint16_t port;
        } ipv6;
        struct
        {
            uint8_t len;
            uint8_t addr[256 + 2];
        } domain;
    };
} __attribute__ ((packed));

struct _Socks5UDPHeader
{
    uint8_t rsv[2];
    uint8_t frag;
    uint8_t atype;
    union
    {
        struct
        {
            uint32_t addr;
            uint16_t port;
        } ipv4;
        struct
        {
            uint8_t addr[16];
            uint16_t port;
        } ipv6;
    };
} __attribute__ ((packed));

static void hev_socks5_session_task_entry (void *data);
static err_t tcp_recv_handler (void *arg, struct tcp_pcb *pcb, struct pbuf *p,
                               err_t err);
static err_t tcp_sent_handler (void *arg, struct tcp_pcb *pcb, u16_t len);
static void tcp_err_handler (void *arg, err_t err);
static void udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port);

static HevSocks5Session *
hev_socks5_session_new (HevTaskMutex *mutex, HevSocks5SessionCloseNotify notify)
{
    HevSocks5Session *self;
    HevTask *task;

    self = hev_malloc0 (sizeof (HevSocks5Session));
    if (!self)
        return NULL;

    self->ref_count = 1;
    self->remote_fd = -1;
    self->notify = notify;
    self->mutex = mutex;
    self->base.hp = DEF_SESSION_HP;

    if (LOG_ON ())
        self->saddr = hev_malloc (SADDR_SIZE);

    task = hev_task_new (TASK_STACK_SIZE);
    if (!task) {
        hev_free (self);
        return NULL;
    }

    self->base.task = task;
    hev_task_set_priority (task, 9);

    return self;
}

HevSocks5Session *
hev_socks5_session_new_tcp (struct tcp_pcb *pcb, HevTaskMutex *mutex,
                            HevSocks5SessionCloseNotify notify)
{
    HevSocks5Session *self;

    self = hev_socks5_session_new (mutex, notify);
    if (!self)
        return NULL;

    self->tcp = pcb;
    self->port = pcb->local_port;
    __builtin_memcpy (&self->addr, &pcb->local_ip, sizeof (ip_addr_t));
    self->type = TYPE_TCP;

    tcp_arg (pcb, self);
    tcp_recv (pcb, tcp_recv_handler);
    tcp_sent (pcb, tcp_sent_handler);
    tcp_err (pcb, tcp_err_handler);

    if (LOG_ON ()) {
        char buf[64];
        const char *sa;
        int port = pcb->remote_port;

        sa = ipaddr_ntoa_r (&pcb->remote_ip, buf, sizeof (buf));
        if (self->saddr)
            snprintf (self->saddr, SADDR_SIZE, "[%s]:%u", sa, port);

        port = pcb->local_port;
        sa = ipaddr_ntoa_r (&pcb->local_ip, buf, sizeof (buf));
        LOG_I ("Session %s: created TCP -> [%s]:%u", self->saddr, sa, port);
    }

    return self;
}

HevSocks5Session *
hev_socks5_session_new_udp (struct udp_pcb *pcb, HevTaskMutex *mutex,
                            HevSocks5SessionCloseNotify notify)
{
    HevSocks5Session *self;

    self = hev_socks5_session_new (mutex, notify);
    if (!self)
        return NULL;

    self->udp = pcb;
    self->type = TYPE_UDP;

    udp_recv (pcb, udp_recv_handler, self);

    if (LOG_ON ()) {
        char buf[64];
        const char *sa;
        int port = pcb->remote_port;

        sa = ipaddr_ntoa_r (&pcb->remote_ip, buf, sizeof (buf));
        if (self->saddr)
            snprintf (self->saddr, SADDR_SIZE, "[%s]:%u", sa, port);

        LOG_I ("Session %s: created UDP", self->saddr);
    }

    return self;
}

HevSocks5Session *
hev_socks5_session_new_dns (struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port,
                            HevTaskMutex *mutex,
                            HevSocks5SessionCloseNotify notify)
{
    HevSocks5Session *self;

    self = hev_socks5_session_new (mutex, notify);
    if (!self)
        return NULL;

    self->udp = pcb;
    self->query = p;
    self->port = port;
    __builtin_memcpy (&self->addr, addr, sizeof (ip_addr_t));
    self->type = TYPE_DNS;

    if (LOG_ON ()) {
        char buf[64];
        const char *sa;

        sa = ipaddr_ntoa_r (addr, buf, sizeof (buf));
        if (self->saddr)
            snprintf (self->saddr, SADDR_SIZE, "[%s]:%u", sa, port);
        LOG_I ("Session %s: created DNS", self->saddr);
    }

    return self;
}

HevSocks5Session *
hev_socks5_session_ref (HevSocks5Session *self)
{
    self->ref_count++;

    return self;
}

void
hev_socks5_session_unref (HevSocks5Session *self)
{
    self->ref_count--;
    if (self->ref_count)
        return;

    hev_free (self);
}

void
hev_socks5_session_run (HevSocks5Session *self)
{
    hev_task_run (self->base.task, hev_socks5_session_task_entry, self);
}

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5Session *self = data;

    if (self->type == TYPE_UDP)
        self->base.hp = UDP_SESSION_HP;
    else
        self->base.hp = DEF_SESSION_HP;

    hev_task_yield (type);

    return (self->base.hp > 0) ? 0 : -1;
}

static int
socks5_do_connect (HevSocks5Session *self)
{
    HevTask *task;
    struct sockaddr *addr;
    socklen_t addr_len;

    self->remote_fd = hev_task_io_socket_socket (AF_INET6, SOCK_STREAM, 0);
    if (self->remote_fd == -1) {
        LOG_W ("Session %s: create remote socket failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    task = hev_task_self ();
    hev_task_add_fd (task, self->remote_fd, POLLIN | POLLOUT);
    addr = hev_config_get_socks5_address (&addr_len);

    if (hev_task_io_socket_connect (self->remote_fd, addr, addr_len,
                                    task_io_yielder, self) < 0) {
        LOG_W ("Session %s: connect remote server failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    return STEP_WRITE_REQUEST;
}

static int
socks5_write_request (HevSocks5Session *self)
{
    Socks5AuthHeader socks5_auth;
    Socks5ReqResHeader socks5_r;
    struct msghdr mh = { 0 };
    struct iovec iov[2];
    ssize_t len;

    mh.msg_iov = iov;
    mh.msg_iovlen = 2;

    /* write socks5 auth method */
    socks5_auth.ver = 0x05;
    socks5_auth.method_len = 0x01;
    socks5_auth.methods[0] = 0x00;
    iov[0].iov_base = &socks5_auth;
    iov[0].iov_len = 3;

    /* write socks5 request */
    socks5_r.ver = 0x05;
    switch (self->type) {
    case TYPE_TCP:
        socks5_r.cmd = 0x01;
        break;
    case TYPE_UDP:
        socks5_r.cmd = 0x05;
        break;
    case TYPE_DNS:
        socks5_r.cmd = 0x04;
        break;
    }
    socks5_r.rsv = 0x00;
    switch (self->addr.type) {
    case IPADDR_TYPE_V4:
        len = 10;
        socks5_r.atype = 0x01;
        socks5_r.ipv4.port = htons (self->port);
        __builtin_memcpy (&socks5_r.ipv4.addr, &self->addr, 4);
        break;
    case IPADDR_TYPE_V6:
        len = 22;
        socks5_r.atype = 0x04;
        socks5_r.ipv6.port = htons (self->port);
        __builtin_memcpy (socks5_r.ipv6.addr, &self->addr, 16);
        break;
    default:
        return STEP_CLOSE_SESSION;
    }
    iov[1].iov_base = &socks5_r;
    iov[1].iov_len = len;

    len = hev_task_io_socket_sendmsg (self->remote_fd, &mh, MSG_WAITALL,
                                      task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: send socks5 request failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    return STEP_READ_RESPONSE;
}

static int
socks5_read_response (HevSocks5Session *self)
{
    Socks5AuthHeader socks5_auth;
    Socks5ReqResHeader socks5_r;
    ssize_t len;

    /* read socks5 auth method */
    len = hev_task_io_socket_recv (self->remote_fd, &socks5_auth, 2,
                                   MSG_WAITALL, task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: receive socks5 response failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }
    /* check socks5 version and auth method */
    if (socks5_auth.ver != 0x05 || socks5_auth.method != 0x00) {
        LOG_W ("Session %s: invalid socks5 response!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    /* read socks5 response header */
    len = hev_task_io_socket_recv (self->remote_fd, &socks5_r, 4, MSG_WAITALL,
                                   task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: receive socks5 response failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    /* check socks5 version, rep */
    if (socks5_r.ver != 0x05 || socks5_r.rep != 0x00) {
        LOG_W ("Session %s: invalid socks5 response!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    switch (socks5_r.atype) {
    case 0x01:
        len = 6;
        break;
    case 0x04:
        len = 18;
        break;
    default:
        LOG_W ("Session %s: address type isn't supported!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    /* read socks5 response body */
    len = hev_task_io_socket_recv (self->remote_fd, &socks5_r, len, MSG_WAITALL,
                                   task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: receive socks5 response failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    switch (self->type) {
    case TYPE_TCP:
        return STEP_DO_SPLICE;
    case TYPE_UDP:
        return STEP_DO_FWD_UDP;
    case TYPE_DNS:
        return STEP_DO_FWD_DNS;
    }

    return STEP_CLOSE_SESSION;
}

static err_t
tcp_recv_handler (void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    HevSocks5Session *self = arg;

    if (!p) {
        self->base.hp = 0;
        goto exit;
    }

    if (!self->queue) {
        self->queue = p;
    } else {
        if (self->queue->tot_len > TCP_WND)
            return ERR_WOULDBLOCK;
        pbuf_cat (self->queue, p);
    }

exit:
    hev_task_wakeup (self->base.task);
    return ERR_OK;
}

static err_t
tcp_sent_handler (void *arg, struct tcp_pcb *pcb, u16_t len)
{
    HevSocks5Session *self = arg;

    hev_task_wakeup (self->base.task);
    return ERR_OK;
}

static void
tcp_err_handler (void *arg, err_t err)
{
    HevSocks5Session *self = arg;

    self->tcp = NULL;
    self->base.hp = 0;
    hev_task_wakeup (self->base.task);
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5Session *self = arg;
    Socks5UDPHeader *udp;
    struct pbuf *b;

    if (!p) {
        self->base.hp = 0;
        goto exit;
    }

    switch (pcb->local_ip.type) {
    case IPADDR_TYPE_V4:
        b = pbuf_alloc (PBUF_TRANSPORT, 12, PBUF_RAM);
        break;
    case IPADDR_TYPE_V6:
        b = pbuf_alloc (PBUF_TRANSPORT, 24, PBUF_RAM);
        break;
    default:
        pbuf_free (p);
        self->base.hp = 0;
        goto exit;
    }

    if (!b) {
        pbuf_free (p);
        self->base.hp = 0;
        goto exit;
    }

    udp = b->payload;
    udp->rsv[0] = 0;
    udp->rsv[1] = 0;
    udp->frag = 0;

    switch (pcb->local_ip.type) {
    case IPADDR_TYPE_V4:
        udp->atype = 0x01;
        udp->ipv4.port = htons (pcb->local_port);
        __builtin_memcpy (&udp->ipv4.addr, &pcb->local_ip, 4);
        *(uint16_t *)(b->payload + 10) = htons (p->tot_len);
        break;
    case IPADDR_TYPE_V6:
        udp->atype = 0x04;
        udp->ipv6.port = htons (pcb->local_port);
        __builtin_memcpy (udp->ipv6.addr, &pcb->local_ip, 16);
        *(uint16_t *)(b->payload + 22) = htons (p->tot_len);
        break;
    }

    if (!self->queue) {
        self->queue = b;
    } else {
        pbuf_cat (self->queue, b);
    }
    pbuf_cat (self->queue, p);

exit:
    hev_task_wakeup (self->base.task);
}

static int
tcp_splice_f (HevSocks5Session *self)
{
    ssize_t s;

    if (!self->queue)
        return 0;

    if (self->queue->next) {
        struct pbuf *p = self->queue;
        struct iovec iov[64];
        int i;

        for (i = 0; (i < 64) && p; p = p->next) {
            iov[i].iov_base = p->payload;
            iov[i].iov_len = p->len;
            i++;
        }

        s = writev (self->remote_fd, iov, i);
    } else {
        s = write (self->remote_fd, self->queue->payload, self->queue->len);
    }

    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    } else {
        hev_task_mutex_lock (self->mutex);
        self->queue = pbuf_free_header (self->queue, s);
        tcp_recved (self->tcp, s);
        hev_task_mutex_unlock (self->mutex);
    }

    return 1;
}

static int
tcp_splice_b (HevSocks5Session *self, uint8_t *buffer)
{
    u8_t flags = TCP_WRITE_FLAG_COPY;
    err_t err = ERR_OK;
    size_t size;
    ssize_t s;

    if (!self->tcp)
        return -1;

    if (!(size = tcp_sndbuf (self->tcp))) {
        hev_task_mutex_lock (self->mutex);
        if (self->tcp)
            err = tcp_output (self->tcp);
        hev_task_mutex_unlock (self->mutex);
        if (!self->tcp || (err != ERR_OK))
            return -1;
        return 0;
    }

    if (size > BUFFER_SIZE)
        size = BUFFER_SIZE;

    s = read (self->remote_fd, buffer, size);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    } else if (s == size) {
        flags |= TCP_WRITE_FLAG_MORE;
    }

    hev_task_mutex_lock (self->mutex);
    if (self->tcp)
        err = tcp_write (self->tcp, buffer, s, flags);
    hev_task_mutex_unlock (self->mutex);
    if (!self->tcp || (err != ERR_OK))
        return -1;

    return 1;
}

static int
socks5_do_splice (HevSocks5Session *self)
{
    uint8_t *buffer;
    int res_f = 1;
    int res_b = 1;

    buffer = hev_malloc (BUFFER_SIZE);
    if (!buffer)
        return STEP_CLOSE_SESSION;

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

        if (task_io_yielder (type, self) < 0)
            break;
    }

    hev_free (buffer);
    return STEP_CLOSE_SESSION;
}

static int
udp_fwd_f (HevSocks5Session *self)
{
    ssize_t s;

    if (!self->queue)
        return 0;

    if (self->queue->next) {
        struct pbuf *p = self->queue;
        struct iovec iov[64];
        int i;

        for (i = 0; (i < 64) && p; p = p->next) {
            iov[i].iov_base = p->payload;
            iov[i].iov_len = p->len;
            i++;
        }

        s = writev (self->remote_fd, iov, i);
    } else {
        s = write (self->remote_fd, self->queue->payload, self->queue->len);
    }

    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    } else {
        hev_task_mutex_lock (self->mutex);
        self->queue = pbuf_free_header (self->queue, s);
        hev_task_mutex_unlock (self->mutex);
    }

    return 1;
}

static int
udp_fwd_b (HevSocks5Session *self)
{
    Socks5UDPHeader udp;
    err_t err = ERR_OK;
    struct pbuf *buf;
    ip_addr_t addr;
    uint16_t port;
    uint16_t len;
    ssize_t s;

    /* peek */
    s = recv (self->remote_fd, udp.rsv, sizeof (udp.rsv), MSG_PEEK);
    if (0 >= s) {
        if ((0 > s) && (EAGAIN == errno))
            return 0;
        return -1;
    }

    /* rsv - atype */
    s = hev_task_io_socket_recv (self->remote_fd, &udp, 4, MSG_WAITALL,
                                 task_io_yielder, self);
    if (0 >= s)
        return -1;

    /* addr */
    switch (udp.atype) {
    case 0x01: /* ipv4 */
        s = hev_task_io_socket_recv (self->remote_fd, &udp.ipv4, 6, MSG_WAITALL,
                                     task_io_yielder, self);
        break;
    case 0x04: /* ipv6 */
        s = hev_task_io_socket_recv (self->remote_fd, &udp.ipv6, 18,
                                     MSG_WAITALL, task_io_yielder, self);
        break;
    default:
        return -1;
    }

    if (0 >= s)
        return -1;

    /* data len */
    s = hev_task_io_socket_recv (self->remote_fd, &len, sizeof (len),
                                 MSG_WAITALL, task_io_yielder, self);
    if (0 >= s)
        return -1;

    len = ntohs (len);
    if (len > 2048)
        return -1;

    hev_task_mutex_lock (self->mutex);
    buf = pbuf_alloc (PBUF_TRANSPORT, len, PBUF_RAM);
    hev_task_mutex_unlock (self->mutex);
    if (!buf)
        return -1;

    /* data */
    s = hev_task_io_socket_recv (self->remote_fd, buf->payload, len,
                                 MSG_WAITALL, task_io_yielder, self);
    if (0 >= s)
        return -1;

    switch (udp.atype) {
    case 0x01: /* ipv4 */
        addr.type = IPADDR_TYPE_V4;
        port = ntohs (udp.ipv4.port);
        __builtin_memcpy (&addr, &udp.ipv4.addr, 4);
        break;
    case 0x04: /* ipv6 */
        addr.type = IPADDR_TYPE_V6;
        port = ntohs (udp.ipv6.port);
        __builtin_memcpy (&addr, udp.ipv6.addr, 16);
        err = udp_sendfrom (self->udp, buf, &addr, udp.ipv6.port);
        break;
    default:
        port = 0;
    }

    hev_task_mutex_lock (self->mutex);
    err = udp_sendfrom (self->udp, buf, &addr, port);
    hev_task_mutex_unlock (self->mutex);

    if (err != ERR_OK) {
        pbuf_free (buf);
        return -1;
    }

    return 1;
}

static int
socks5_do_fwd_udp (HevSocks5Session *self)
{
    int res_f = 1;
    int res_b = 1;

    for (;;) {
        HevTaskYieldType type;

        if (res_f >= 0)
            res_f = udp_fwd_f (self);
        if (res_b >= 0)
            res_b = udp_fwd_b (self);

        if (res_f < 0 || res_b < 0)
            break;
        else if (res_f > 0 || res_b > 0)
            type = HEV_TASK_YIELD;
        else
            type = HEV_TASK_WAITIO;

        if (task_io_yielder (type, self) < 0)
            break;
    }

    return STEP_CLOSE_SESSION;
}

static int
socks5_do_fwd_dns (HevSocks5Session *self)
{
    struct msghdr mh = { 0 };
    struct iovec iov[2];
    struct pbuf *buf;
    uint16_t dns_len;
    ssize_t len;

    mh.msg_iov = iov;
    mh.msg_iovlen = 2;

    /* write dns request length */
    dns_len = htons (self->query->tot_len);
    iov[0].iov_base = &dns_len;
    iov[0].iov_len = 2;

    /* write dns request */
    iov[1].iov_base = self->query->payload;
    iov[1].iov_len = self->query->tot_len;

    /* send dns request */
    len = hev_task_io_socket_sendmsg (self->remote_fd, &mh, MSG_WAITALL,
                                      task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: send DNS request failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    /* read dns response length */
    len = hev_task_io_socket_recv (self->remote_fd, &dns_len, 2, MSG_WAITALL,
                                   task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: receive DNS response failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }
    dns_len = ntohs (dns_len);

    /* check dns response length */
    if (dns_len >= 2048) {
        LOG_W ("Session %s: DNS response is invalid!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    hev_task_mutex_lock (self->mutex);
    buf = pbuf_alloc (PBUF_TRANSPORT, dns_len, PBUF_RAM);
    hev_task_mutex_unlock (self->mutex);
    if (!buf) {
        LOG_W ("Session %s: alloc dns buffer failed!", self->saddr);
        return STEP_CLOSE_SESSION;
    }

    /* read dns response */
    len = hev_task_io_socket_recv (self->remote_fd, buf->payload, dns_len,
                                   MSG_WAITALL, task_io_yielder, self);
    if (len <= 0) {
        LOG_W ("Session %s: receive DNS response failed!", self->saddr);
        hev_task_mutex_lock (self->mutex);
        pbuf_free (buf);
        hev_task_mutex_unlock (self->mutex);
        return STEP_CLOSE_SESSION;
    }

    /* send dns response */
    hev_task_mutex_lock (self->mutex);
    if (udp_sendto (self->udp, buf, &self->addr, self->port) != ERR_OK)
        pbuf_free (buf);
    hev_task_mutex_unlock (self->mutex);

    return STEP_CLOSE_SESSION;
}

static int
socks5_close_session (HevSocks5Session *self)
{
    if (self->remote_fd >= 0)
        close (self->remote_fd);

    hev_task_mutex_lock (self->mutex);
    switch (self->type) {
    case TYPE_TCP:
        if (self->tcp) {
            tcp_recv (self->tcp, NULL);
            tcp_sent (self->tcp, NULL);
            tcp_err (self->tcp, NULL);
            if (tcp_close (self->tcp) != ERR_OK)
                tcp_abort (self->tcp);
        }
        if (self->queue)
            pbuf_free (self->queue);
        break;
    case TYPE_UDP:
        if (self->udp) {
            udp_remove (self->udp);
        }
        if (self->queue)
            pbuf_free (self->queue);
        break;
    case TYPE_DNS:
        pbuf_free (self->query);
        break;
    }
    hev_task_mutex_unlock (self->mutex);

    LOG_I ("Session %s: closed", self->saddr);

    if (self->saddr)
        hev_free (self->saddr);

    self->notify (self);
    hev_socks5_session_unref (self);

    return STEP_NULL;
}

static void
hev_socks5_session_task_entry (void *data)
{
    HevSocks5Session *self = data;
    int step = STEP_DO_CONNECT;

    while (step) {
        switch (step) {
        case STEP_DO_CONNECT:
            step = socks5_do_connect (self);
            break;
        case STEP_WRITE_REQUEST:
            step = socks5_write_request (self);
            break;
        case STEP_READ_RESPONSE:
            step = socks5_read_response (self);
            break;
        case STEP_DO_SPLICE:
            step = socks5_do_splice (self);
            break;
        case STEP_DO_FWD_UDP:
            step = socks5_do_fwd_udp (self);
            break;
        case STEP_DO_FWD_DNS:
            step = socks5_do_fwd_dns (self);
            break;
        case STEP_CLOSE_SESSION:
            step = socks5_close_session (self);
            break;
        default:
            step = STEP_NULL;
            break;
        }
    }
}
