/*
 ============================================================================
 Name        : hev-tunnel-linux.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel on Linux
 ============================================================================
 */

#ifndef __HEV_TUNNEL_LINUX_H__
#define __HEV_TUNNEL_LINUX_H__

static inline ssize_t
hev_tunnel_read (int fd, void *buf, size_t count, HevTaskIOYielder yielder,
                 void *yielder_data)
{
    return hev_task_io_read (fd, buf, count, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_readv (int fd, struct iovec *iov, int iovcnt,
                  HevTaskIOYielder yielder, void *yielder_data)
{
    return hev_task_io_readv (fd, &iov[1], iovcnt - 1, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_write (int fd, void *buf, size_t count, HevTaskIOYielder yielder,
                  void *yielder_data)
{
    return hev_task_io_write (fd, buf, count, yielder, yielder_data);
}

static inline ssize_t
hev_tunnel_writev (int fd, struct iovec *iov, int iovcnt,
                   HevTaskIOYielder yielder, void *yielder_data)
{
    return hev_task_io_writev (fd, &iov[1], iovcnt - 1, yielder, yielder_data);
}

#endif /* __HEV_TUNNEL_LINUX_H__ */
