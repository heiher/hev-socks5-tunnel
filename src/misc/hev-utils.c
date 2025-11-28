/*
 ============================================================================
 Name        : hev-utils.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Utils
 ============================================================================
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include <hev-socks5-misc.h>

#include "hev-logger.h"
#include "hev-mapped-dns.h"

#include "hev-utils.h"

void
run_as_daemon (const char *pid_file)
{
    FILE *fp;

    fp = fopen (pid_file, "w+");
    if (!fp) {
        LOG_E ("open pid file %s", pid_file);
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#if !(TARGET_OS_TV)
    if (daemon (0, 0)) {
        /* ignore return value */
    }
#endif
#pragma GCC diagnostic pop

    fprintf (fp, "%u\n", getpid ());
    fclose (fp);
}

int
set_limit_nofile (int limit_nofile)
{
    struct rlimit limit;
    int res;

    res = getrlimit (RLIMIT_NOFILE, &limit);
    if (res < 0)
        return -1;

    limit.rlim_cur = limit.rlim_max;
    res = setrlimit (RLIMIT_NOFILE, &limit);
    if (res < 0)
        return -1;

    limit.rlim_cur = limit_nofile;
    limit.rlim_max = limit_nofile;
    return setrlimit (RLIMIT_NOFILE, &limit);
}

int
set_sock_mark (int fd, unsigned int mark)
{
#if defined(__linux__)
    return setsockopt (fd, SOL_SOCKET, SO_MARK, &mark, sizeof (mark));
#elif defined(__FreeBSD__)
    return setsockopt (fd, SOL_SOCKET, SO_USER_COOKIE, &mark, sizeof (mark));
#endif
    return 0;
}

int
hev_socks5_addr_from_lwip (HevSocks5Addr *addr, const ip_addr_t *ip, u16_t port)
{
    switch (ip->type) {
    case IPADDR_TYPE_V4: {
        HevMappedDNS *dns = hev_mapped_dns_get ();
        const char *name = NULL;
        if (dns)
            name = hev_mapped_dns_lookup (dns, ntohl (ip_2_ip4 (ip)->addr));
        if (name)
            hev_socks5_addr_from_name (addr, name, htons (port));
        else
            hev_socks5_addr_from_ipv4 (addr, ip, htons (port));
        return 0;
    }
    case IPADDR_TYPE_V6:
        hev_socks5_addr_from_ipv6 (addr, ip, htons (port));
        return 0;
    default:
        return -1;
    }
}

int
hev_socks5_addr_into_lwip (const HevSocks5Addr *addr, ip_addr_t *ip,
                           u16_t *port)
{
    switch (addr->atype) {
    case HEV_SOCKS5_ADDR_TYPE_IPV4:
        memcpy (ip, addr->ipv4.addr, 4);
        *port = ntohs (addr->ipv4.port);
        ip->type = IPADDR_TYPE_V4;
        return 0;
    case HEV_SOCKS5_ADDR_TYPE_IPV6:
        memcpy (ip, addr->ipv6.addr, 16);
        *port = ntohs (addr->ipv6.port);
        ip->type = IPADDR_TYPE_V6;
        return 0;
    default:
        return -1;
    }
}
