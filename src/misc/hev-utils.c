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

#include "hev-logger.h"

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
    struct rlimit limit = {
        .rlim_cur = limit_nofile,
        .rlim_max = limit_nofile,
    };

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
lwip_to_sock_addr (const ip_addr_t *ip, u16_t port, struct sockaddr *addr)
{
    if (ip->type == IPADDR_TYPE_V4) {
        struct sockaddr_in *adp;

        adp = (struct sockaddr_in *)addr;
        adp->sin_family = AF_INET;
        adp->sin_port = htons (port);
        memcpy (&adp->sin_addr, ip, 4);
    } else if (ip->type == IPADDR_TYPE_V6) {
        struct sockaddr_in6 *adp;

        adp = (struct sockaddr_in6 *)addr;
        adp->sin6_family = AF_INET6;
        adp->sin6_port = htons (port);
        memcpy (&adp->sin6_addr, ip, 16);
    }

    return 0;
}

int
sock_to_lwip_addr (const struct sockaddr *addr, ip_addr_t *ip, u16_t *port)
{
    if (addr->sa_family == AF_INET) {
        struct sockaddr_in *adp;

        adp = (struct sockaddr_in *)addr;
        ip->type = IPADDR_TYPE_V4;
        *port = ntohs (adp->sin_port);
        memcpy (ip, &adp->sin_addr, 4);
    } else if (addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *adp;

        adp = (struct sockaddr_in6 *)addr;
        ip->type = IPADDR_TYPE_V6;
        *port = ntohs (adp->sin6_port);
        memcpy (ip, &adp->sin6_addr, 16);
    } else {
        return -1;
    }

    return 0;
}
