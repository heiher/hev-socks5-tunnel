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
#include <hev-task.h>
#include <hev-socks5-proto.h>

void run_as_daemon (const char *pid_file);
int set_limit_nofile (int limit_nofile);
int set_sock_mark (int fd, unsigned int mark);
void set_sock_tcp_fastopen (int fd, int enable);

/*
 * A yielder that never waits, the I/O fails instead of yielding the current
 * task. For use outside of the task context, e.g. other threads and signal
 * handlers.
 */
static inline int
hev_task_io_nowait (HevTaskYieldType type, void *data)
{
    return -1;
}

int hev_socks5_addr_from_lwip (HevSocks5Addr *addr, const ip_addr_t *ip,
                               u16_t port);
int hev_socks5_addr_into_lwip (const HevSocks5Addr *addr, ip_addr_t *ip,
                               u16_t *port);

#endif /* __HEV_UTILS_H__ */
