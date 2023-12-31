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
#include <netinet/in.h>

void run_as_daemon (const char *pid_file);
int set_limit_nofile (int limit_nofile);
int set_sock_mark (int fd, unsigned int mark);
int lwip_to_sock_addr (const ip_addr_t *ip, u16_t port, struct sockaddr *addr);
int sock_to_lwip_addr (const struct sockaddr *addr, ip_addr_t *ip, u16_t *port);

#endif /* __HEV_UTILS_H__ */
