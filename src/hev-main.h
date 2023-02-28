/*
 ============================================================================
 Name        : hev-main.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Main
 ============================================================================
 */

#ifndef __HEV_MAIN_H__
#define __HEV_MAIN_H__

int hev_socks5_tunnel_main (const char *config_path, int tun_fd);
void hev_socks5_tunnel_quit (void);

#endif /* __HEV_MAIN_H__ */
