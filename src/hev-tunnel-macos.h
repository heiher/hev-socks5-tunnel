/*
 ============================================================================
 Name        : hev-tunnel-macos.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel on MacOS
 ============================================================================
 */

#ifndef __HEV_TUNNEL_MACOS_H__
#define __HEV_TUNNEL_MACOS_H__

#include <sys/socket.h>

static inline ssize_t
hev_tunnel_read (int fd, void *buf, size_t count, HevTaskIOYielder yielder,
                 void *yielder_data)
{
    struct iovec iov[2];
    uint32_t type;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);
    iov[1].iov_base = buf;
    iov[1].iov_len = count;

    return hev_task_io_readv (fd, iov, 2, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_readv (int fd, struct iovec *iov, int iovcnt,
                  HevTaskIOYielder yielder, void *yielder_data)
{
    uint32_t type;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);

    return hev_task_io_readv (fd, iov, iovcnt, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_write (int fd, void *buf, size_t count, HevTaskIOYielder yielder,
                  void *yielder_data)
{
    struct iovec iov[2];
    uint8_t *iph = buf;
    uint32_t type;

    if (((iph[0] >> 4) & 0xF) == 4)
        type = htonl (AF_INET);
    else
        type = htonl (AF_INET6);

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);
    iov[1].iov_base = buf;
    iov[1].iov_len = count;

    return hev_task_io_writev (fd, iov, 2, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_writev (int fd, struct iovec *iov, int iovcnt,
                   HevTaskIOYielder yielder, void *yielder_data)
{
    uint32_t type;
    int i;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);

    for (i = 1; i < iovcnt; i++) {
        if (iov[i].iov_len) {
            uint8_t *iph = iov[i].iov_base;

            if (((iph[0] >> 4) & 0xF) == 4)
                type = htonl (AF_INET);
            else
                type = htonl (AF_INET6);

            break;
        }
    }

    return hev_task_io_writev (fd, iov, iovcnt, yielder, yielder_data);
}

#endif /* __HEV_TUNNEL_MACOS_H__ */
