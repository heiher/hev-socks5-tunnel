/*
 ============================================================================
 Name        : hev-utils.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Utils
 ============================================================================
 */

#ifndef __HEV_UTILS_H__
#define __HEV_UTILS_H__

#include <lwip/ip_addr.h>
#include <hev-socks5-proto.h>

void run_as_daemon (const char *pid_file);
int set_limit_nofile (int limit_nofile);
int set_sock_mark (int fd, unsigned int mark);

int hev_socks5_addr_from_lwip (HevSocks5Addr *addr, const ip_addr_t *ip,
                               u16_t port);
int hev_socks5_addr_into_lwip (const HevSocks5Addr *addr, ip_addr_t *ip,
                               u16_t *port);

#endif /* __HEV_UTILS_H__ */
