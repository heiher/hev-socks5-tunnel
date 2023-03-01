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
#include <net/if_utun.h>
#include <sys/sys_domain.h>
#include <sys/kern_control.h>
#endif

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-tunnel.h"

static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name)
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

#endif /* __APPLE__ || __MACH__ */
