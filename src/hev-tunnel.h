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

#if defined(__MSYS__)
#include "hev-tunnel-windows.h"
#endif /* __MSYS__ */

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__)
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
#endif /* defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) */

int hev_tunnel_open (const char *name, int multi_queue);
void hev_tunnel_close (int fd);

int hev_tunnel_set_mtu (int mtu);
int hev_tunnel_set_state (int state);

int hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix);
int hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix);

const char *hev_tunnel_get_name (void);
const char *hev_tunnel_get_index (void);

int hev_tunnel_add_task (int fd, HevTask *task);
void hev_tunnel_del_task (int fd, HevTask *task);

#endif /* __HEV_TUNNEL_H__ */
