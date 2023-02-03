/*
 ============================================================================
 Name        : hev-tunnel-macos.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel on MacOS
 ============================================================================
 */

#if defined(__APPLE__) || defined(__MACH__)

#include <stdio.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <net/if.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if_utun.h>
#include <netinet6/in6_var.h>

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-tunnel.h"

static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name)
{
    socklen_t len = IFNAMSIZ;
    struct sockaddr_ctl sc;
    struct ctl_info ci;
    int nonblock = 1;
    int res = -1;
    int fd;

    memset (&ci, 0, sizeof (ci));
    strncpy (ci.ctl_name, UTUN_CONTROL_NAME, sizeof (ci.ctl_name));

    fd = socket (PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0)
        goto exit;

    res = ioctl (fd, CTLIOCGINFO, &ci);
    if (res < 0)
        goto exit_close;

    sc.sc_id = ci.ctl_id;
    sc.sc_len = sizeof (sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_unit = 0;

    res = connect (fd, (struct sockaddr *)&sc, sizeof (sc));
    if (res < 0)
        goto exit_close;

    res = ioctl (fd, FIONBIO, (char *)&nonblock);
    if (res < 0)
        goto exit_close;

    res = getsockopt (fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, tun_name, &len);
    if (res < 0)
        goto exit_close;

    return fd;

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_mtu (int mtu)
{
    struct ifreq ifr = { .ifr_mtu = mtu };
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCSIFMTU, (void *)&ifr);

    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_state (int state)
{
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCGIFFLAGS, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    if (state)
        ifr.ifr_flags |= IFF_UP;
    else
        ifr.ifr_flags &= ~IFF_UP;
    res = ioctl (fd, SIOCSIFFLAGS, (void *)&ifr);

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix)
{
    /*
     * TODO: Set IPv4 address
     */

    return 0;
}

int
hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix)
{
    /*
     * TODO: Set IPv6 address
     */

    return 0;
}

ssize_t
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

ssize_t
hev_tunnel_readv (int fd, struct iovec *iov, int iovcnt,
                  HevTaskIOYielder yielder, void *yielder_data)
{
    uint32_t type;

    iov[0].iov_base = &type;
    iov[0].iov_len = sizeof (type);

    return hev_task_io_readv (fd, iov, iovcnt, yielder, yielder_data);
}

ssize_t
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

ssize_t
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

#endif /* __APPLE__ || __MACH__ */
