/*
 ============================================================================
 Name        : hev-logger.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Logger
 ============================================================================
 */

#ifndef __HEV_LOGGER_H__
#define __HEV_LOGGER_H__

/*
 * MSVC C mode has no GNU named variadic macros (fmt...), so the standard
 * __VA_ARGS__ form is used there.  Everywhere else the GNU form is kept so
 * the preprocessed output of the affected translation units does not change.
 */
#if defined(_MSC_VER)
#define LOG_D(...) hev_logger_log (HEV_LOGGER_DEBUG, __VA_ARGS__)
#define LOG_I(...) hev_logger_log (HEV_LOGGER_INFO, __VA_ARGS__)
#define LOG_W(...) hev_logger_log (HEV_LOGGER_WARN, __VA_ARGS__)
#define LOG_E(...) hev_logger_log (HEV_LOGGER_ERROR, __VA_ARGS__)
#else
#define LOG_D(fmt...) hev_logger_log (HEV_LOGGER_DEBUG, fmt)
#define LOG_I(fmt...) hev_logger_log (HEV_LOGGER_INFO, fmt)
#define LOG_W(fmt...) hev_logger_log (HEV_LOGGER_WARN, fmt)
#define LOG_E(fmt...) hev_logger_log (HEV_LOGGER_ERROR, fmt)
#endif

#define LOG_ON() hev_logger_enabled (HEV_LOGGER_UNSET)
#define LOG_ON_D() hev_logger_enabled (HEV_LOGGER_DEBUG)
#define LOG_ON_I() hev_logger_enabled (HEV_LOGGER_INFO)
#define LOG_ON_W() hev_logger_enabled (HEV_LOGGER_WARN)
#define LOG_ON_E() hev_logger_enabled (HEV_LOGGER_ERROR)

typedef enum
{
    HEV_LOGGER_DEBUG,
    HEV_LOGGER_INFO,
    HEV_LOGGER_WARN,
    HEV_LOGGER_ERROR,
    HEV_LOGGER_UNSET,
} HevLoggerLevel;

int hev_logger_init (HevLoggerLevel level, const char *path);
void hev_logger_fini (void);

int hev_logger_enabled (HevLoggerLevel level);
void hev_logger_log (HevLoggerLevel level, const char *fmt, ...);

#endif /* __HEV_LOGGER_H__ */
