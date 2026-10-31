/*
 ============================================================================
 Name        : hev-compat.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2026 hev
 Description : Compatibility
 ============================================================================
 */

#ifndef __HEV_COMPAT_H__
#define __HEV_COMPAT_H__

#if defined(_WIN32) && !defined(__MSYS__) && !defined(__CYGWIN__)
#define HEV_WIN_NATIVE 1
#endif

#ifdef HEV_WIN_NATIVE

#include <hev-sys.h>
#include <windows.h>

#define strcasecmp _stricmp
#define usleep(us) Sleep (((us) + 999) / 1000)

#endif

#endif /* __HEV_COMPAT_H__ */
