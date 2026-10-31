/*
 ============================================================================
 Name        : hev-exec.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Exec
 ============================================================================
 */

#include <stdlib.h>

#include "hev-compat.h"

#ifdef HEV_WIN_NATIVE
#include <stdio.h>
#else
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

#if defined(__APPLE__)
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include "hev-logger.h"

#include "hev-exec.h"

#ifdef HEV_WIN_NATIVE
void
hev_exec_run (const char *script_path, const char *tun_name,
              const char *tun_index, int wait)
{
    STARTUPINFOA si = { 0 };
    PROCESS_INFORMATION pi = { 0 };
    char cmd[2048];
    int len;

    len = snprintf (cmd, sizeof (cmd), "\"%s\" \"%s\" \"%s\"", script_path,
                    tun_name ? tun_name : "", tun_index ? tun_index : "");
    if (len < 0 || len >= (int)sizeof (cmd)) {
        LOG_E ("exec %s %s %s", script_path, tun_name, tun_index);
        return;
    }

    si.cb = sizeof (si);
    if (!CreateProcessA (NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                         &pi)) {
        LOG_E ("exec %s %s %s", script_path, tun_name, tun_index);
        return;
    }

    if (wait)
        WaitForSingleObject (pi.hProcess, INFINITE);

    CloseHandle (pi.hThread);
    CloseHandle (pi.hProcess);
}
#elif TARGET_OS_TV
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
