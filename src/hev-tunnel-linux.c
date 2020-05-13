/*
 ============================================================================
 Name        : hev-tunnel-linux.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2020 Everyone.
 Description : Tunnel on Linux
 ============================================================================
 */

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/ipv6.h>
#include <linux/if.h>
#include <linux/if_tun.h>

#include "hev-task.h"
#include "hev-task-io.h"
#include "hev-memory-allocator.h"

#include "hev-tunnel-linux.h"

struct _HevTunnelLinux
{
    int fd;
    char name[IFNAMSIZ];
};

HevTunnelLinux *
hev_tunnel_linux_new (const char *name)
{
    HevTunnelLinux *self;
    struct ifreq ifr = { 0 };

    self = hev_malloc0 (sizeof (HevTunnelLinux));
    if (!self)
        goto exit;

    self->fd = hev_task_io_open ("/dev/net/tun", O_RDWR);
    if (self->fd < 0)
        goto exit_free;

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (name)
        strncpy (ifr.ifr_name, name, IFNAMSIZ - 1);

    if (ioctl (self->fd, TUNSETIFF, (void *)&ifr) < 0)
        goto exit_close;

    memcpy (self->name, ifr.ifr_name, IFNAMSIZ);
    return self;

exit_close:
    close (self->fd);
exit_free:
    hev_free (self);
exit:
    return NULL;
}

void
hev_tunnel_linux_destroy (HevTunnelLinux *self)
{
    close (self->fd);
    hev_free (self);
}

int
hev_tunnel_linux_set_mtu (HevTunnelLinux *self, int mtu)
{
    int fd, res = -1;
    struct ifreq ifr = { .ifr_mtu = mtu };

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, self->name, IFNAMSIZ);
    res = ioctl (fd, SIOCSIFMTU, (void *)&ifr);

    close (fd);
exit:
    return res;
}

int
hev_tunnel_linux_set_ipv4 (HevTunnelLinux *self, const char *addr,
                           unsigned int prefix)
{
    int fd, res = -1;
    struct ifreq ifr = { 0 };
    struct sockaddr_in *pa;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, self->name, IFNAMSIZ);

    pa = (struct sockaddr_in *)&ifr.ifr_addr;
    pa->sin_family = AF_INET;
    if (!inet_pton (AF_INET, addr, &pa->sin_addr))
        goto exit_close;
    if (ioctl (fd, SIOCSIFADDR, (void *)&ifr) < 0)
        goto exit_close;

    pa = (struct sockaddr_in *)&ifr.ifr_netmask;
    pa->sin_family = AF_INET;
    pa->sin_addr.s_addr = htonl (((unsigned int)(-1)) << (32 - prefix));
    res = ioctl (fd, SIOCSIFNETMASK, (void *)&ifr);

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_linux_set_ipv6 (HevTunnelLinux *self, const char *addr,
                           unsigned int prefix)
{
    int fd, res = -1;
    struct ifreq ifr = { 0 };
    struct in6_ifreq ifr6 = { 0 };

    fd = socket (AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, self->name, IFNAMSIZ);
    if (ioctl (fd, SIOCGIFINDEX, (void *)&ifr) < 0)
        goto exit_close;

    ifr6.ifr6_prefixlen = prefix;
    ifr6.ifr6_ifindex = ifr.ifr_ifindex;
    if (!inet_pton (AF_INET6, addr, &ifr6.ifr6_addr))
        goto exit_close;
    res = ioctl (fd, SIOCSIFADDR, (void *)&ifr6);

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_linux_set_state (HevTunnelLinux *self, int state)
{
    int fd, res = -1;
    struct ifreq ifr = { 0 };

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, self->name, IFNAMSIZ);
    if (ioctl (fd, SIOCGIFFLAGS, (void *)&ifr) < 0)
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
hev_tunnel_linux_get_fd (HevTunnelLinux *self)
{
    return self->fd;
}
