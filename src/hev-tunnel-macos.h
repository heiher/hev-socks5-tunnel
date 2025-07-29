/*
 ============================================================================
 Name        : hev-tunnel-macos.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 - 2025 hev
 Description : Tunnel on MacOS
 ============================================================================
 */

#ifndef __HEV_TUNNEL_MACOS_H__
#define __HEV_TUNNEL_MACOS_H__

#include <sys/socket.h>

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
    if (s <= sizeof (type)) {
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
    if (res <= sizeof (type))
        return -1;

    return res;
}

#endif /* __HEV_TUNNEL_MACOS_H__ */
