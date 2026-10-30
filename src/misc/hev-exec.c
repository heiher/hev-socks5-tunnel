/*
 ============================================================================
 Name        : hev-exec.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Exec
 ============================================================================
 */

#include <stdlib.h>
#if defined(_MSC_VER)
#include <string.h>
#include <winsock2.h>
#include <windows.h>
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

#if defined(_MSC_VER)

static wchar_t *
hev_win_utf8_to_wide (const char *str)
{
    wchar_t *wstr;
    int len;

    len = MultiByteToWideChar (CP_UTF8, 0, str, -1, NULL, 0);
    if (len <= 0)
        return NULL;

    wstr = malloc (len * sizeof (wchar_t));
    if (!wstr)
        return NULL;

    if (!MultiByteToWideChar (CP_UTF8, 0, str, -1, wstr, len)) {
        free (wstr);
        return NULL;
    }

    return wstr;
}

/* Quote argv per CommandLineToArgvW rules; UTF-8 bytes are quoted safely. */
static size_t
hev_exec_arg_size (const char *arg)
{
    size_t n = 2; /* surrounding quotes */
    size_t slashes = 0;

    for (; *arg; arg++) {
        if (*arg == '\\') {
            slashes++;
            continue;
        }

        if (*arg == '"')
            n += slashes * 2 + 2; /* escaped slashes + escaped quote */
        else
            n += slashes + 1;
        slashes = 0;
    }

    n += slashes * 2;
    return n;
}

static char *
hev_exec_write_arg (char *dst, const char *arg)
{
    size_t slashes = 0;

    *dst++ = '"';
    for (; *arg; arg++) {
        if (*arg == '\\') {
            slashes++;
            continue;
        }

        if (*arg == '"') {
            size_t i;
            for (i = 0; i < slashes * 2 + 1; i++)
                *dst++ = '\\';
            *dst++ = '"';
        } else {
            size_t i;
            for (i = 0; i < slashes; i++)
                *dst++ = '\\';
            *dst++ = *arg;
        }
        slashes = 0;
    }

    while (slashes-- > 0) {
        *dst++ = '\\';
        *dst++ = '\\';
    }
    *dst++ = '"';
    return dst;
}

/* Case-insensitive test for a file name suffix. */
static int
hev_exec_has_suffix (const char *path, const char *suffix)
{
    size_t plen = strlen (path);
    size_t slen = strlen (suffix);
    size_t i;

    if (plen < slen)
        return 0;

    for (i = 0; i < slen; i++) {
        char c = path[plen - slen + i];

        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');

        if (c != suffix[i])
            return 0;
    }

    return 1;
}

/* .bat/.cmd are executed by cmd.exe, which re-parses the command line. */
static int
hev_exec_is_cmd_script (const char *path)
{
    return hev_exec_has_suffix (path, ".bat") ||
           hev_exec_has_suffix (path, ".cmd");
}

/* .bat/.cmd are executed by cmd.exe; refuse args containing cmd metachars. */
static int
hev_exec_cmd_safe (const char *arg)
{
    static const char meta[] = "\"%&|<>^!";

    for (; *arg; arg++) {
        if (strchr (meta, *arg))
            return 0;
    }

    return 1;
}

/* cmd.exe /d /s /c strips the outer quotes and runs the script verbatim. */
static const char hev_exec_cmd_prefix[] = "cmd.exe /d /s /c \"";

void
hev_exec_run (const char *script_path, const char *tun_name,
              const char *tun_index, int wait)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    const char *name = tun_name ? tun_name : "";
    const char *index = tun_index ? tun_index : "";
    const char *prefix = "";
    wchar_t *wcmdline = NULL;
    size_t plen = 0, extra, size, total;
    char *cmdline = NULL;
    char *pos;
    int is_cmd;

    if (!script_path || !*script_path)
        return;

    is_cmd = hev_exec_is_cmd_script (script_path);

    if (is_cmd) {
        if (!(hev_exec_cmd_safe (script_path) && hev_exec_cmd_safe (name) &&
              hev_exec_cmd_safe (index))) {
            LOG_E ("exec %s unsafe for the command interpreter", script_path);
            return;
        }
        prefix = hev_exec_cmd_prefix;
        plen = sizeof (hev_exec_cmd_prefix) - 1;
    }

    /* +3: two separating spaces and the terminating NUL. */
    extra = plen + 3 + (is_cmd ? 1 : 0);

    size = hev_exec_arg_size (script_path);
    if (size > SIZE_MAX - extra) {
        LOG_E ("exec command line too long");
        return;
    }
    size += extra;
    extra = hev_exec_arg_size (name);
    if (extra > SIZE_MAX - size) {
        LOG_E ("exec command line too long");
        return;
    }
    size += extra;
    extra = hev_exec_arg_size (index);
    if (extra > SIZE_MAX - size) {
        LOG_E ("exec command line too long");
        return;
    }
    size += extra;
    total = size;

    cmdline = malloc (total);
    if (!cmdline) {
        LOG_E ("exec command line allocation");
        return;
    }

    pos = cmdline;
    if (plen) {
        memcpy (pos, prefix, plen);
        pos += plen;
    }
    pos = hev_exec_write_arg (pos, script_path);
    *pos++ = ' ';
    pos = hev_exec_write_arg (pos, name);
    *pos++ = ' ';
    pos = hev_exec_write_arg (pos, index);
    if (is_cmd)
        *pos++ = '"';
    *pos = '\0';

    wcmdline = hev_win_utf8_to_wide (cmdline);
    free (cmdline);
    if (!wcmdline) {
        LOG_E ("exec command line conversion");
        return;
    }

    memset (&si, 0, sizeof (si));
    si.cb = sizeof (si);
    memset (&pi, 0, sizeof (pi));

    if (!CreateProcessW (NULL, wcmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                         &pi)) {
        LOG_E ("exec %s %s %s", script_path, name, index);
        free (wcmdline);
        return;
    }

    free (wcmdline);

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
