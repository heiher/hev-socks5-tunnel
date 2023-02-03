/*
 ============================================================================
 Name        : hev-tunnel.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel
 ============================================================================
 */

#ifndef __HEV_TUNNEL_H__
#define __HEV_TUNNEL_H__

int hev_tunnel_open (const char *name);

int hev_tunnel_set_mtu (int mtu);
int hev_tunnel_set_state (int state);

int hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix);
int hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix);

ssize_t hev_tunnel_read (int fd, void *buf, size_t count,
                         HevTaskIOYielder yielder, void *yielder_data);
ssize_t hev_tunnel_readv (int fd, struct iovec *iov, int iovcnt,
                          HevTaskIOYielder yielder, void *yielder_data);

ssize_t hev_tunnel_write (int fd, void *buf, size_t count,
                          HevTaskIOYielder yielder, void *yielder_data);
ssize_t hev_tunnel_writev (int fd, struct iovec *iov, int iovcnt,
                           HevTaskIOYielder yielder, void *yielder_data);

#endif /* __HEV_TUNNEL_H__ */
