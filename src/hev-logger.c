/*
 ============================================================================
 Name        : hev-logger.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 everyone.
 Description : Logger
 ============================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "hev-config.h"

#include "hev-logger.h"

static int fd = -1;
static HevLoggerLevel req_level;

int
hev_logger_init (void)
{
    const char *str;

    str = hev_config_get_misc_log_file ();
    if (!str)
        return 0;

    if (0 == strcmp ("stdout", str))
        fd = 1;
    else if (0 == strcmp ("stderr", str))
        fd = 2;
    else
        fd = open (str, O_WRONLY | O_APPEND | O_CREAT, 0640);

    if (fd < 0) {
        fprintf (stderr, "Open log file %s failed!\n", str);
        return -1;
    }

    str = hev_config_get_misc_log_level ();
    if (0 == strcmp (str, "debug"))
        req_level = HEV_LOGGER_DEBUG;
    else if (0 == strcmp (str, "info"))
        req_level = HEV_LOGGER_INFO;
    else if (0 == strcmp (str, "error"))
        req_level = HEV_LOGGER_ERROR;
    else
        req_level = HEV_LOGGER_WARN;

    return 0;
}

void
hev_logger_fini (void)
{
    if (fd <= 2)
        return;

    close (fd);
}

int
hev_logger_enabled (HevLoggerLevel level)
{
    if (fd >= 0 || level >= req_level)
        return 1;

    return 0;
}

void
hev_logger_log (HevLoggerLevel level, const char *fmt, ...)
{
    va_list ap;
    time_t now;
    struct tm *ti;
    char ts[32];
    char msg[1024];
    struct iovec iov[4];

    if (fd < 0 || level < req_level)
        return;

    time (&now);
    ti = localtime (&now);

    iov[0].iov_base = ts;
    iov[0].iov_len = snprintf (ts, 32, "[%04u-%02u-%02u %02u:%02u:%02u] ",
                               ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday,
                               ti->tm_hour, ti->tm_min, ti->tm_sec);

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
