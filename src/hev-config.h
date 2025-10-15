/*
 ============================================================================
 Name        : hev-config.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

typedef struct _HevConfigServer HevConfigServer;

struct _HevConfigServer
{
    const char *user;
    const char *pass;
    unsigned int mark;
    short udp_in_udp;
    unsigned short port;
    unsigned char pipeline;
    char udp_addr[256];
    char addr[256];
};

int hev_config_init_from_file (const char *config_path);
int hev_config_init_from_str (const unsigned char *config_str,
                              unsigned int config_len);
void hev_config_fini (void);

const char *hev_config_get_tunnel_name (void);
unsigned int hev_config_get_tunnel_mtu (void);
int hev_config_get_tunnel_multi_queue (void);

const char *hev_config_get_tunnel_ipv4_address (void);
const char *hev_config_get_tunnel_ipv6_address (void);

const char *hev_config_get_tunnel_post_up_script (void);
const char *hev_config_get_tunnel_pre_down_script (void);

HevConfigServer *hev_config_get_socks5_server (void);

int hev_config_get_mapdns_address (void);
int hev_config_get_mapdns_port (void);
int hev_config_get_mapdns_network (void);
int hev_config_get_mapdns_netmask (void);
int hev_config_get_mapdns_cache_size (void);

int hev_config_get_misc_task_stack_size (void);
int hev_config_get_misc_tcp_buffer_size (void);
int hev_config_get_misc_udp_recv_buffer_size (void);
int hev_config_get_misc_udp_copy_buffer_nums (void);
int hev_config_get_misc_max_session_count (void);
int hev_config_get_misc_connect_timeout (void);
int hev_config_get_misc_tcp_read_write_timeout (void);
int hev_config_get_misc_udp_read_write_timeout (void);
int hev_config_get_misc_limit_nofile (void);
const char *hev_config_get_misc_pid_file (void);
const char *hev_config_get_misc_log_file (void);
int hev_config_get_misc_log_level (void);

#endif /* __HEV_CONFIG_H__ */
