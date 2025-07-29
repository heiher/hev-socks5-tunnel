/*
 ============================================================================
 Name        : hev-exec.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2023 - 2025 hev
 Description : Exec
 ============================================================================
 */

#ifndef __HEV_EXEC_H__
#define __HEV_EXEC_H__

void hev_exec_run (const char *script_path, const char *tun_name,
                   const char *tun_index, int wait);

#endif /* __HEV_EXEC_H__ */
