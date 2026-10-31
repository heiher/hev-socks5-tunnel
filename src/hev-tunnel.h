/*
 ============================================================================
 Name        : hev-tunnel.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 - 2025 hev
 Description : Tunnel
 ============================================================================
 */

#ifndef __HEV_TUNNEL_H__
#define __HEV_TUNNEL_H__

enum
{
    HEV_TUNNEL_OFFLOAD_CSUM = 1 << 0,
    HEV_TUNNEL_OFFLOAD_TCP = 1 << 1,
    HEV_TUNNEL_OFFLOAD_UDP = 1 << 2,
    HEV_TUNNEL_OFFLOAD_JUMBO = 1 << 3,
};

#include <hev-sys.h>
#include <lwip/pbuf.h>

#if defined(__linux__)
#include "hev-tunnel-linux.h"
#endif /* __linux__ */

#if defined(__FreeBSD__)
#include "hev-tunnel-freebsd.h"
#endif /* __FreeBSD__ */

#if defined(__NetBSD__)
#include "hev-tunnel-netbsd.h"
#endif /* __NetBSD__ */

#if defined(__APPLE__) || defined(__MACH__)
#include "hev-tunnel-macos.h"
#endif /* __APPLE__ || __MACH__ */

#ifdef _WIN32
#include "hev-tunnel-windows.h"
#endif /* _WIN32 */

#if defined(HEV_TUNNEL_GENERIC_HEAD)
static inline struct pbuf *
hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder, void *yielder_data)
{
    struct iovec iov[2];
    struct pbuf *buf;
    uint32_t type;
    ssize_t s;

    buf = pbuf_alloc (PBUF_RAW, mtu, PBUF_RAM);
    if (!buf)
        return NULL;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);
    iov[1].iov_base = buf->payload;
    iov[1].iov_len = buf->len;

    s = hev_task_io_readv (fd, iov, 2, yielder, yielder_data);
    if (s <= (ssize_t)sizeof (type)) {
        pbuf_free (buf);
        return NULL;
    }

    buf->tot_len = s - sizeof (type);
    buf->len = s - sizeof (type);

    return buf;
}

static inline ssize_t
hev_tunnel_write (int fd, struct pbuf *buf)
{
    struct iovec iov[512];
    struct pbuf *p = buf;
    uint32_t type = 0;
    ssize_t res;
    int i;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);

    for (i = 1; p && (i < 512); p = p->next) {
        iov[i].iov_base = p->payload;
        iov[i].iov_len = p->len;
        i++;

        if (!type && p->len) {
            if (((*(uint8_t *)p->payload >> 4) & 0xF) == 4)
                type = htonl (AF_INET);
            else
                type = htonl (AF_INET6);
        }
    }

    res = writev (fd, iov, i);
    if (res <= (ssize_t)sizeof (type))
        return -1;

    return res;
}

#elif defined(HEV_TUNNEL_GENERIC)
static inline struct pbuf *
hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder, void *yielder_data)
{
    struct pbuf *buf;
    ssize_t s;

    buf = pbuf_alloc (PBUF_RAW, mtu, PBUF_RAM);
    if (!buf)
        return NULL;

    s = hev_task_io_read (fd, buf->payload, buf->len, yielder, yielder_data);
    if (s <= 0) {
        pbuf_free (buf);
        return NULL;
    }

    buf->tot_len = s;
    buf->len = s;

    return buf;
}

static inline ssize_t
hev_tunnel_write (int fd, struct pbuf *buf)
{
    struct iovec iov[512];
    struct pbuf *p = buf;
    int i;

    if (!p->next)
        return write (fd, p->payload, p->len);

    for (i = 0; p && (i < 512); p = p->next) {
        iov[i].iov_base = p->payload;
        iov[i].iov_len = p->len;
        i++;
    }

    return writev (fd, iov, i);
}
#endif /* HEV_TUNNEL_GENERIC */

int hev_tunnel_open (const char *name, int multi_queue, const char *guid);
void hev_tunnel_close (int fd);

void hev_tunnel_set_offload (int fd, int offload);
int hev_tunnel_get_offload (void);

int hev_tunnel_set_mtu (int mtu);
int hev_tunnel_set_state (int state);

int hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix);
int hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix);

const char *hev_tunnel_get_name (void);
const char *hev_tunnel_get_index (void);

int hev_tunnel_add_task (int fd, HevTask *task);
void hev_tunnel_del_task (int fd, HevTask *task);

#endif /* __HEV_TUNNEL_H__ */
