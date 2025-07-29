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
#include <TargetConditionals.h>

#if TARGET_OS_OSX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/in_var.h>
#include <netinet6/nd6.h>
#include <net/if_utun.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#endif

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-tunnel.h"

static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name, int multi_queue)
{
#if TARGET_OS_OSX
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

    res = sscanf (name, "utun%u", &sc.sc_unit);
    if (res > 0)
        sc.sc_unit += 1;

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
#else
    return 0;
#endif
}

void
hev_tunnel_close (int fd)
{
    close (fd);
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
#if TARGET_OS_OSX
    struct ifaliasreq ifra = { 0 };
    struct sockaddr_in *pa;
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        goto exit;

    strcpy (ifra.ifra_name, tun_name);

    pa = (struct sockaddr_in *)&ifra.ifra_addr;
    pa->sin_len = sizeof (ifra.ifra_addr);
    pa->sin_family = AF_INET;
    res = inet_pton (AF_INET, addr, &pa->sin_addr);
    if (!res)
        goto exit_close;

    memcpy (&ifra.ifra_broadaddr, &ifra.ifra_addr, sizeof (ifra.ifra_addr));

    pa = (struct sockaddr_in *)&ifra.ifra_mask;
    pa->sin_len = sizeof (ifra.ifra_addr);
    pa->sin_family = AF_INET;
    pa->sin_addr.s_addr = htonl (((unsigned int)(-1)) << (32 - prefix));

    res = ioctl (fd, SIOCAIFADDR, &ifra);

exit_close:
    close (fd);
exit:
    return res;
#else
    return 0;
#endif
}

int
hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix)
{
#if TARGET_OS_OSX
    struct in6_aliasreq ifra = { .ifra_lifetime = { 0, 0, ND6_INFINITE_LIFETIME,
                                                    ND6_INFINITE_LIFETIME } };
    uint8_t *bytes;
    int res = -1;
    int fd;
    int i;

    fd = socket (AF_INET6, SOCK_DGRAM, 0);
    if (fd < 0)
        goto exit;

    strcpy (ifra.ifra_name, tun_name);

    ifra.ifra_addr.sin6_len = sizeof (ifra.ifra_addr);
    ifra.ifra_addr.sin6_family = AF_INET6;
    res = inet_pton (AF_INET6, addr, &ifra.ifra_addr.sin6_addr);
    if (!res)
        goto exit_close;

    ifra.ifra_prefixmask.sin6_len = sizeof (ifra.ifra_prefixmask);
    ifra.ifra_prefixmask.sin6_family = AF_INET6;
    bytes = (uint8_t *)&ifra.ifra_prefixmask.sin6_addr;
    memset (bytes, 0xFF, 16);
    bytes[prefix / 8] <<= prefix % 8;
    prefix += prefix % 8;
    for (i = prefix / 8; i < 16; i++)
        bytes[i] = 0;

    res = ioctl (fd, SIOCAIFADDR_IN6, &ifra);

exit_close:
    close (fd);
exit:
    return res;
#else
    return 0;
#endif
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

#endif /* __APPLE__ || __MACH__ */
