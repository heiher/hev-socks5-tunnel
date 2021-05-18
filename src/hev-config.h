/*
 ============================================================================
 Name        : hev-config.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2021 hev
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

int hev_config_init (const char *config_path);
void hev_config_fini (void);

const char *hev_config_get_tunnel_name (void);
unsigned int hev_config_get_tunnel_mtu (void);

const char *hev_config_get_tunnel_ipv4_address (void);
const char *hev_config_get_tunnel_ipv4_gateway (void);
unsigned int hev_config_get_tunnel_ipv4_prefix (void);

const char *hev_config_get_tunnel_ipv6_address (void);
const char *hev_config_get_tunnel_ipv6_gateway (void);
unsigned int hev_config_get_tunnel_ipv6_prefix (void);

const char *hev_config_get_socks5_address (int *port);

int hev_config_get_misc_limit_nofile (void);
const char *hev_config_get_misc_pid_file (void);
const char *hev_config_get_misc_log_file (void);
int hev_config_get_misc_log_level (void);

#endif /* __HEV_CONFIG_H__ */
