/*
 ============================================================================
 Name        : hev-tunnel-windows.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Tunnel on Windows
 ============================================================================
 */

#ifndef __HEV_TUNNEL_WINDOWS_H__
#define __HEV_TUNNEL_WINDOWS_H__

ssize_t hev_tunnel_read (int fd, void *buf, size_t count,
                         HevTaskIOYielder yielder, void *yielder_data);
ssize_t hev_tunnel_readv (int fd, struct iovec *iov, int iovcnt,
                          HevTaskIOYielder yielder, void *yielder_data);

ssize_t hev_tunnel_write (int fd, void *buf, size_t count);
ssize_t hev_tunnel_writev (int fd, struct iovec *iov, int iovcnt);

#endif /* __HEV_TUNNEL_WINDOWS_H__ */
