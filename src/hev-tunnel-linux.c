/*
 ============================================================================
 Name        : hev-tunnel-linux.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Tunnel on Linux
 ============================================================================
 */

#if defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/ipv6.h>
#include <linux/if_tun.h>

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-tunnel.h"

static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name, int multi_queue)
{
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = hev_task_io_open ("/dev/net/tun", O_RDWR);
    if (fd < 0)
        goto exit;

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (multi_queue)
        ifr.ifr_flags |= IFF_MULTI_QUEUE;
    if (name)
        strncpy (ifr.ifr_name, name, IFNAMSIZ - 1);

    res = ioctl (fd, TUNSETIFF, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    memcpy (tun_name, ifr.ifr_name, IFNAMSIZ);
    return fd;

exit_close:
    close (fd);
exit:
    return res;
}

void
hev_tunnel_close (int fd)
{
    close (fd);
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
    struct ifreq ifr = { 0 };
    struct sockaddr_in *pa;
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);

    pa = (struct sockaddr_in *)&ifr.ifr_addr;
    pa->sin_family = AF_INET;
    res = inet_pton (AF_INET, addr, &pa->sin_addr);
    if (!res)
        goto exit_close;
    res = ioctl (fd, SIOCSIFADDR, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    pa = (struct sockaddr_in *)&ifr.ifr_netmask;
    pa->sin_family = AF_INET;
    pa->sin_addr.s_addr = htonl (((unsigned int)(-1)) << (32 - prefix));
    res = ioctl (fd, SIOCSIFNETMASK, (void *)&ifr);
    if ((res < 0) && (errno == EEXIST))
        res = 0;

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix)
{
    struct in6_ifreq ifr6 = { 0 };
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = socket (AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCGIFINDEX, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    ifr6.ifr6_prefixlen = prefix;
    ifr6.ifr6_ifindex = ifr.ifr_ifindex;
    res = inet_pton (AF_INET6, addr, &ifr6.ifr6_addr);
    if (!res)
        goto exit_close;
    res = ioctl (fd, SIOCSIFADDR, (void *)&ifr6);
    if ((res < 0) && (errno == EEXIST))
        res = 0;

exit_close:
    close (fd);
exit:
    return res;
}

const char *
hev_tunnel_get_name (void)
{
    return tun_name;
}

const char *
hev_tunnel_get_index (void)
{
    static char tun_index[16];
    unsigned int index;

    index = if_nametoindex (tun_name);
    snprintf (tun_index, sizeof (tun_index) - 1, "%d", index);
    return tun_index;
}

int
hev_tunnel_add_task (int fd, HevTask *task)
{
    return hev_task_add_fd (task, fd, POLLIN);
}

void
hev_tunnel_del_task (int fd, HevTask *task)
{
    hev_task_del_fd (task, fd);
}

#endif /* __linux__ */
