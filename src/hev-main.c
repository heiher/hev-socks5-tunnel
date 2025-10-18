/*
 ============================================================================
 Name        : hev-main.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Main
 ============================================================================
 */

#include <stdio.h>
#include <signal.h>
#include <string.h>

#include <lwip/init.h>

#include <hev-task.h>
#include <hev-task-system.h>
#include <hev-socks5-misc.h>

#include "hev-utils.h"
#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-logger.h"
#include "hev-socks5-logger.h"
#include "hev-socks5-tunnel.h"

#include "hev-main.h"

static int
hev_socks5_tunnel_main_inner (int tun_fd)
{
    const char *pid_file;
    const char *log_file;
    int log_level;
    int nofile;
    int res;

    log_file = hev_config_get_misc_log_file ();
    log_level = hev_config_get_misc_log_level ();

    res = hev_config_get_misc_connect_timeout ();
    hev_socks5_set_connect_timeout (res);
    res = hev_config_get_misc_tcp_read_write_timeout ();
    hev_socks5_set_tcp_timeout (res);
    res = hev_config_get_misc_udp_read_write_timeout ();
    hev_socks5_set_udp_timeout (res);

    res = hev_config_get_misc_udp_recv_buffer_size ();
    hev_socks5_set_udp_recv_buffer_size (res);

    res = hev_logger_init (log_level, log_file);
    if (res < 0)
        return -2;

    res = hev_socks5_logger_init (log_level, log_file);
    if (res < 0)
        return -3;

    nofile = hev_config_get_misc_limit_nofile ();
    res = set_limit_nofile (nofile);
    if (res < 0)
        LOG_I ("set limit nofile");

    pid_file = hev_config_get_misc_pid_file ();
    if (pid_file)
        run_as_daemon (pid_file);

    res = hev_task_system_init ();
    if (res < 0)
        return -4;

    lwip_init ();

    res = hev_socks5_tunnel_init (tun_fd);
    if (res < 0)
        return -5;

    hev_socks5_tunnel_run ();

    hev_socks5_tunnel_fini ();
    hev_socks5_logger_fini ();
    hev_logger_fini ();
    hev_config_fini ();
    hev_task_system_fini ();

    return 0;
}

int
hev_socks5_tunnel_main_from_file (const char *config_path, int tun_fd)
{
    int res = hev_config_init_from_file (config_path);
    if (res < 0)
        return -1;

    return hev_socks5_tunnel_main_inner (tun_fd);
}

int
hev_socks5_tunnel_main_from_str (const unsigned char *config_str,
                                 unsigned int config_len, int tun_fd)
{
    int res = hev_config_init_from_str (config_str, config_len);
    if (res < 0)
        return -1;

    return hev_socks5_tunnel_main_inner (tun_fd);
}

int
hev_socks5_tunnel_main (const char *config_path, int tun_fd)
{
    return hev_socks5_tunnel_main_from_file (config_path, tun_fd);
}

void
hev_socks5_tunnel_quit (void)
{
    hev_socks5_tunnel_stop ();
}

#ifndef ENABLE_LIBRARY
static void
show_help (const char *self_path)
{
    printf ("%s CONFIG_PATH\n", self_path);
    printf ("Version: %u.%u.%u %s\n", MAJOR_VERSION, MINOR_VERSION,
            MICRO_VERSION, COMMIT_ID);
}

static void
sigint_handler (int signum)
{
    hev_socks5_tunnel_stop ();
}

int
main (int argc, char *argv[])
{
    int res;

    if (argc < 2 || strcmp (argv[1], "--version") == 0) {
        show_help (argv[0]);
        return -1;
    }

    signal (SIGINT, sigint_handler);

    res = hev_socks5_tunnel_main (argv[1], -1);
    if (res < 0)
        return -2;

    return 0;
}
#endif /* ENABLE_LIBRARY */
