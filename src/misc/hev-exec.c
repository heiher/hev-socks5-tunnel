/*
 ============================================================================
 Name        : hev-exec.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Exec
 ============================================================================
 */

#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#if defined(__APPLE__)
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include "hev-logger.h"

#include "hev-exec.h"

#if TARGET_OS_TV
void
hev_exec_run (const char *script_path, const char *tun_name,
              const char *tun_index, int wait)
{
}
#else
static void
signal_handler (int signum)
{
    waitpid (-1, NULL, WNOHANG);
}

void
hev_exec_run (const char *script_path, const char *tun_name,
              const char *tun_index, int wait)
{
    pid_t pid;

    signal (SIGCHLD, signal_handler);

    pid = fork ();
    if (pid < 0) {
        LOG_E ("exec fork");
        return;
    } else if (pid != 0) {
        if (wait)
            waitpid (pid, NULL, 0);
        return;
    }

    execl (script_path, script_path, tun_name, tun_index, NULL);

    LOG_E ("exec %s %s %s", script_path, tun_name, tun_index);
    exit (-1);
}
#endif
