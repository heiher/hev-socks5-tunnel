/*
 ============================================================================
 Name        : hev-main.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 everyone.
 Description : Main
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

#include "lwip/init.h"
#include "lwip/priv/tcp_priv.h"

#include "hev-task.h"
#include "hev-task-system.h"
#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-logger.h"
#include "hev-socks5-tunnel.h"

#include "hev-main.h"

static void
show_help (const char *self_path)
{
    printf ("%s CONFIG_PATH [TUN_FD]\n", self_path);
    printf ("Version: %u.%u.%u\n", MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION);
}

static void
run_as_daemon (const char *pid_file)
{
    FILE *fp;

    fp = fopen (pid_file, "w+");
    if (!fp) {
        LOG_E ("Open pid file %s failed!", pid_file);
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (daemon (0, 0)) {
        /* ignore return value */
    }
#pragma GCC diagnostic pop

    fprintf (fp, "%u", getpid ());
    fclose (fp);
}

static int
set_limit_nofile (int limit_nofile)
{
    struct rlimit limit = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    if (-1 > limit_nofile)
        return 0;

    if (0 <= limit_nofile) {
        limit.rlim_cur = limit_nofile;
        limit.rlim_max = limit_nofile;
    }

    return setrlimit (RLIMIT_NOFILE, &limit);
}

static void
lwip_fini (void)
{
    int i;

    for (i = 0; i < NUM_TCP_PCB_LISTS; i++) {
        struct tcp_pcb *cpcb;

        for (cpcb = *tcp_pcb_lists[i]; cpcb;) {
            struct tcp_pcb *p = cpcb;

            cpcb = cpcb->next;
            tcp_abort (p);
        }
    }
}

int
main (int argc, char *argv[])
{
    const char *pid_file;
    int tun_fd = -1, limit_nofile;

    if (2 > argc) {
        show_help (argv[0]);
        return -1;
    }

    if (2 < argc)
        tun_fd = strtol (argv[2], NULL, 10);

    if (0 > hev_config_init (argv[1]))
        return -2;

    if (0 > hev_logger_init ())
        return -3;

    limit_nofile = hev_config_get_misc_limit_nofile ();
    if (0 > set_limit_nofile (limit_nofile)) {
        LOG_E ("Set limit nofile failed!");
        return -4;
    }

    lwip_init ();

    if (0 > hev_socks5_tunnel_init (tun_fd))
        return -5;

    pid_file = hev_config_get_misc_pid_file ();
    if (pid_file)
        run_as_daemon (pid_file);

    hev_socks5_tunnel_run ();

    hev_socks5_tunnel_fini ();
    hev_logger_fini ();
    hev_config_fini ();
    lwip_fini ();

    return 0;
}

void
quit (void)
{
    hev_socks5_tunnel_stop ();
}
