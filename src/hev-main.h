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

/**
 * hev_socks5_tunnel_main:
 * @config_path: config file path
 * @tun_fd: tunnel file descriptor
 *
 * Start and run the socks5 tunnel, this function will blocks until the
 * hev_socks5_tunnel_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.4.6
 */
int hev_socks5_tunnel_main (const char *config_path, int tun_fd);

/**
 * hev_socks5_tunnel_quit:
 *
 * Stop the socks5 tunnel.
 *
 * Since: 2.4.6
 */
void hev_socks5_tunnel_quit (void);

#endif /* __HEV_MAIN_H__ */
