/*
 ============================================================================
 Name        : hev-logger.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Logger
 ============================================================================
 */

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#if defined(_MSC_VER)
#include <io.h>
#include <hev-sys.h>
#else
#include <unistd.h>
#include <sys/uio.h>
#endif
#include <sys/stat.h>

#include "hev-logger.h"

static int fd = -1;

#if defined(_MSC_VER)
#define hev_logger_dup(n) _dup (n)
#define hev_logger_open(path)     _open (path, _O_WRONLY | _O_APPEND | _O_CREAT | _O_BINARY,            _S_IREAD | _S_IWRITE)
#define hev_logger_close(fd) _close (fd)

static int
hev_logger_writev (int fd, const struct iovec *iov, int iovcnt)
{
    int i, res = 0;

    for (i = 0; i < iovcnt; i++) {
        res = _write (fd, iov[i].iov_base, (unsigned int)iov[i].iov_len);
        if (res < 0)
            break;
    }

    return res;
}
#else
#define hev_logger_dup(n) fcntl (n, F_DUPFD_CLOEXEC, 0)
#define hev_logger_open(path)     open (path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0640)
#define hev_logger_close(fd) close (fd)
#define hev_logger_writev writev
#endif
static HevLoggerLevel req_level;

int
hev_logger_init (HevLoggerLevel level, const char *path)
{
    if (!path)
        return 0;

    req_level = level;

    if (0 == strcmp (path, "stdout"))
        fd = hev_logger_dup (1);
    else if (0 == strcmp (path, "stderr"))
        fd = hev_logger_dup (2);
    else
        fd = hev_logger_open (path);

    if (fd < 0)
        return -1;

    return 0;
}

void
hev_logger_fini (void)
{
    if (fd >= 0) {
        hev_logger_close (fd);
        fd = -1;
    }
}

int
hev_logger_enabled (HevLoggerLevel level)
{
    if (level >= req_level && fd >= 0)
        return 1;

    return 0;
}

void
hev_logger_log (HevLoggerLevel level, const char *fmt, ...)
{
    struct iovec iov[4];
    const char *ts_fmt;
    char msg[1024];
    struct tm *ti;
    char ts[32];
    time_t now;
    va_list ap;
    int len;

    if (level < req_level || fd < 0)
        return;

    time (&now);
    ti = localtime (&now);

    ts_fmt = "[%04u-%02u-%02u %02u:%02u:%02u] ";
    len = snprintf (ts, sizeof (ts), ts_fmt, 1900 + ti->tm_year, 1 + ti->tm_mon,
                    ti->tm_mday, ti->tm_hour, ti->tm_min, ti->tm_sec);

    iov[0].iov_base = ts;
    iov[0].iov_len = len;

    switch (level) {
    case HEV_LOGGER_DEBUG:
        iov[1].iov_base = "[D] ";
        break;
    case HEV_LOGGER_INFO:
        iov[1].iov_base = "[I] ";
        break;
    case HEV_LOGGER_WARN:
        iov[1].iov_base = "[W] ";
        break;
    case HEV_LOGGER_ERROR:
        iov[1].iov_base = "[E] ";
        break;
    case HEV_LOGGER_UNSET:
        iov[1].iov_base = "[?] ";
        break;
    }
    iov[1].iov_len = 4;

    va_start (ap, fmt);
    iov[2].iov_base = msg;
    iov[2].iov_len = vsnprintf (msg, sizeof (msg), fmt, ap);
    if (iov[2].iov_len >= sizeof (msg))
        iov[2].iov_len = sizeof (msg) - 1;
    va_end (ap);

    iov[3].iov_base = "\n";
    iov[3].iov_len = 1;

    if (hev_logger_writev (fd, iov, 4)) {
        /* ignore return value */
    }
}
