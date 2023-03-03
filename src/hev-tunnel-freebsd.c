/*
 ============================================================================
 Name        : hev-tunnel-freebsd.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Tunnel on FreeBSD
 ============================================================================
 */

#if defined(__FreeBSD__)

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <net/if_tun.h>

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-tunnel.h"

static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name)
{
    struct ifreq ifr;
    char path[256];
    int res = -1;
    int fd;

    snprintf (path, sizeof (path), "/dev/%s", name);
    fd = hev_task_io_open (path, O_RDWR);
    if (fd >= 0) {
        memcpy (tun_name, name, IFNAMSIZ);
        return fd;
    }

    fd = hev_task_io_open ("/dev/tun", O_RDWR);
    if (fd < 0)
        goto exit;

    res = ioctl (fd, TUNGIFNAME, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    memcpy (tun_name, ifr.ifr_name, IFNAMSIZ);
    return fd;

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_mtu (int mtu)
{
    int fd, res = -1;
    struct ifreq ifr = { .ifr_mtu = mtu };

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

#endif /* __FreeBSD__ */
