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
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>

#include "hev-logger.h"

static int fd = -1;
static HevLoggerLevel req_level;

int
hev_logger_init (HevLoggerLevel level, const char *path)
{
    req_level = level;

    if (0 == strcmp (path, "stdout"))
        fd = dup (1);
    else if (0 == strcmp (path, "stderr"))
        fd = dup (2);
    else
        fd = open (path, O_WRONLY | O_APPEND | O_CREAT, 0640);

    if (fd < 0)
        return -1;

    return 0;
}

void
hev_logger_fini (void)
{
    close (fd);
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
    iov[2].iov_len = vsnprintf (msg, 1024, fmt, ap);
    va_end (ap);

    iov[3].iov_base = "\n";
    iov[3].iov_len = 1;

    if (writev (fd, iov, 4)) {
        /* ignore return value */
    }
}
