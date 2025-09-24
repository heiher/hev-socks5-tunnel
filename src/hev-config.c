/*
 ============================================================================ 
 Name        : hev-config.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2024 hev
 Description : Config
 ============================================================================ 
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <lwip/tcp.h>
#include <yaml.h>

#include <hev-compiler.h> // Added for EXPORT_SYMBOL
#include <hev-logger.h>
#include <hev-config.h>
#include <hev-config-const.h>

static char tun_name[64];
static unsigned int tun_mtu = 8500;
static int multi_queue;

static char tun_ipv4_address[16];
static char tun_ipv6_address[64];

static char tun_post_up_script[1024];
static char tun_pre_down_script[1024];

static HevConfigServer srv_tcp, srv_udp;

static int mapdns_address;
static int mapdns_port;
static int mapdns_network;
static int mapdns_netmask;
static int mapdns_cache_size;

static char log_file[1024];
static char pid_file[1024];
static int task_stack_size = 86016;
static int tcp_buffer_size = 65536;
static int connect_timeout = 5000;
static int read_write_timeout = 60000;
static int limit_nofile = 65535;
static int log_level = HEV_LOGGER_WARN;

static char fwd_virtual_ip4[64];
static char fwd_virtual_ip6[64];
static char fwd_target4[128];
static char fwd_target6[128];

static int chnroutes_enabled = 0;
static char chnroutes_file_path[1024];
static int smart_proxy_enabled = 0;
static unsigned int smart_proxy_timeout_ms = 0;
static unsigned int smart_proxy_blocked_ip_expiry_minutes = 0;

static int
hev_config_parse_tunnel_ipv4 (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "address"))
            strncpy (tun_ipv4_address, value, 16 - 1);
    }

    return 0;
}

static int
hev_config_parse_tunnel_ipv6 (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "address"))
            strncpy (tun_ipv6_address, value, 64 - 1);
    }

    return 0;
}

static int
hev_config_parse_tunnel (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node)
            break;

        if (YAML_SCALAR_NODE == node->type) {
            const char *value = (const char *)node->data.scalar.value;

            if (0 == strcmp (key, "name"))
                strncpy (tun_name, value, 64 - 1);
            else if (0 == strcmp (key, "mtu"))
                tun_mtu = strtoul (value, NULL, 10);
            else if (0 == strcmp (key, "multi-queue"))
                multi_queue = strcasecmp (value, "false");
            else if (0 == strcmp (key, "ipv4"))
                strncpy (tun_ipv4_address, value, 16 - 1);
            else if (0 == strcmp (key, "ipv6"))
                strncpy (tun_ipv6_address, value, 64 - 1);
            else if (0 == strcmp (key, "post-up-script"))
                strncpy (tun_post_up_script, value, 64 - 1);
            else if (0 == strcmp (key, "pre-down-script"))
                strncpy (tun_pre_down_script, value, 64 - 1);
        } else {
            if (0 == strcmp (key, "ipv4"))
                hev_config_parse_tunnel_ipv4 (doc, node);
            else if (0 == strcmp (key, "ipv6"))
                hev_config_parse_tunnel_ipv6 (doc, node);
        }
    }

    return 0;
}

static int
hev_config_parse_socks5_server (yaml_document_t *doc, yaml_node_t *base,
                                HevConfigServer *srv, int is_udp)
{
    yaml_node_pair_t *pair;
    static char _user[2][256];
    static char _pass[2][256];
    const char *addr = NULL;
    const char *port = NULL;
    const char *udpm = NULL;
    const char *user = NULL;
    const char *pass = NULL;
    const char *mark = NULL;
    const char *pipe = NULL;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "port"))
            port = value;
        else if (0 == strcmp (key, "address"))
            addr = value;
        else if (0 == strcmp (key, "udp") || 0 == strcmp (key, "udp-relay"))
            udpm = value;
        else if (0 == strcmp (key, "pipeline"))
            pipe = value;
        else if (0 == strcmp (key, "username"))
            user = value;
        else if (0 == strcmp (key, "password"))
            pass = value;
        else if (0 == strcmp (key, "mark"))
            mark = value;
    }

    if (!port) {
        fprintf (stderr, "Can\'t found socks5.%s.port!\n", is_udp ? "udp" : "tcp");
        return -1;
    }

    if (!addr) {
        fprintf (stderr, "Can\'t found socks5.%s.address!\n", is_udp ? "udp" : "tcp");
        return -1;
    }

    if ((user && !pass) || (!user && pass)) {
        fprintf (stderr, "Must be set both socks5 username and password!\n");
        return -1;
    }

    strncpy (srv->addr, addr, 256 - 1);
    srv->port = strtoul (port, NULL, 10);

    if (pipe && (strcasecmp (pipe, "true") == 0))
        srv->pipeline = 1;

    if (udpm && (strcasecmp (udpm, "udp") == 0))
        srv->udp_in_udp = 1;

    if (user && pass) {
        char *u = is_udp ? _user[1] : _user[0];
        char *p = is_udp ? _pass[1] : _pass[0];
        strncpy (u, user, 256 - 1);
        strncpy (p, pass, 256 - 1);
        srv->user = u;
        srv->pass = p;
    }

    if (mark)
        srv->mark = strtoul (mark, NULL, 0);

    return 0;
}

static int
hev_config_parse_socks5 (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    yaml_node_t *tcp_node = NULL;
    yaml_node_t *udp_node = NULL;
    int res_tcp = -1, res_udp = -1;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            continue;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (0 == strcmp (key, "tcp"))
            tcp_node = node;
        else if (0 == strcmp (key, "udp") || 0 == strcmp (key, "udp-relay"))
            udp_node = node;
    }

    if (tcp_node)
        res_tcp = hev_config_parse_socks5_server (doc, tcp_node, &srv_tcp, 0);

    if (udp_node)
        res_udp = hev_config_parse_socks5_server (doc, udp_node, &srv_udp, 1);

    if (res_tcp < 0 && res_udp < 0) {
        res_tcp = hev_config_parse_socks5_server (doc, base, &srv_tcp, 0);
        if (res_tcp < 0)
            return -1;
        memcpy (&srv_udp, &srv_tcp, sizeof (HevConfigServer));
    } else if (res_tcp < 0) {
        memcpy (&srv_tcp, &srv_udp, sizeof (HevConfigServer));
    } else if (res_udp < 0) {
        memcpy (&srv_udp, &srv_tcp, sizeof (HevConfigServer));
    }

    return 0;
}

static int
hev_config_parse_mapdns (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "address"))
            inet_pton (AF_INET, value, &mapdns_address);
        else if (0 == strcmp (key, "port"))
            mapdns_port = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "network"))
            inet_pton (AF_INET, value, &mapdns_network);
        else if (0 == strcmp (key, "netmask"))
            inet_pton (AF_INET, value, &mapdns_netmask);
        else if (0 == strcmp (key, "cache-size"))
            mapdns_cache_size = strtoul (value, NULL, 10);
    }

    mapdns_network = ntohl (mapdns_network);
    mapdns_netmask = ntohl (mapdns_netmask);

    return 0;
}

static int
hev_config_parse_log_level (const char *value)
{
    if (0 == strcmp (value, "debug"))
        return HEV_LOGGER_DEBUG;
    else if (0 == strcmp (value, "info"))
        return HEV_LOGGER_INFO;
    else if (0 == strcmp (value, "error"))
        return HEV_LOGGER_ERROR;

    return HEV_LOGGER_WARN;
}

static int
hev_config_parse_dns_forwarder (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "virtual-ip4"))
            strncpy (fwd_virtual_ip4, value, 64 - 1);
        else if (0 == strcmp (key, "virtual-ip6"))
            strncpy (fwd_virtual_ip6, value, 64 - 1);
        else if (0 == strcmp (key, "target-ip4"))
            strncpy (fwd_target4, value, 128 - 1);
        else if (0 == strcmp (key, "target-ip6"))
            strncpy (fwd_target6, value, 128 - 1);
    }

    return 0;
}

static int
hev_config_parse_smart_proxy (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "timeout-ms"))
            smart_proxy_timeout_ms = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "blocked-ip-expiry-minutes"))
            smart_proxy_blocked_ip_expiry_minutes = strtoul (value, NULL, 10);
    }

    if (smart_proxy_timeout_ms > 0 && smart_proxy_blocked_ip_expiry_minutes > 0) {
        smart_proxy_enabled = 1;
    }

    return 0;
}

static int
hev_config_parse_misc (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "task-stack-size"))
            task_stack_size = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "tcp-buffer-size"))
            tcp_buffer_size = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "connect-timeout"))
            connect_timeout = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "read-write-timeout"))
            read_write_timeout = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "pid-file"))
            strncpy (pid_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-file"))
            strncpy (log_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-level"))
            log_level = hev_config_parse_log_level (value);
        else if (0 == strcmp (key, "limit-nofile"))
            limit_nofile = strtol (value, NULL, 10);
    }

    return 0;
}

static int
hev_config_parse_doc (yaml_document_t *doc)
{
    yaml_node_t *root;
    yaml_node_pair_t *pair;
    int min_task_stack_size;

    root = yaml_document_get_root_node (doc);
    if (!root || YAML_MAPPING_NODE != root->type)
        return -1;

    for (pair = root->data.mapping.pairs.start;
         pair < root->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key;
        int res = 0;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;

        key = (const char *)node->data.scalar.value;
        node = yaml_document_get_node (doc, pair->value);

        if (0 == strcmp (key, "tunnel"))
            res = hev_config_parse_tunnel (doc, node);
        else if (0 == strcmp (key, "socks5"))
            res = hev_config_parse_socks5 (doc, node);
        else if (0 == strcmp (key, "mapdns"))
            res = hev_config_parse_mapdns (doc, node);
        else if (0 == strcmp (key, "dns-forwarder"))
            res = hev_config_parse_dns_forwarder (doc, node);
        else if (0 == strcmp (key, "misc"))
            res = hev_config_parse_misc (doc, node);
        else if (0 == strcmp (key, "chnroutes")) {
            if (YAML_MAPPING_NODE == node->type) {
                yaml_node_pair_t *chnroutes_pair;
                for (chnroutes_pair = node->data.mapping.pairs.start;
                     chnroutes_pair < node->data.mapping.pairs.top; chnroutes_pair++) {
                    yaml_node_t *chnroutes_key_node = yaml_document_get_node(doc, chnroutes_pair->key);
                    yaml_node_t *chnroutes_value_node = yaml_document_get_node(doc, chnroutes_pair->value);
                    if (chnroutes_key_node && YAML_SCALAR_NODE == chnroutes_key_node->type &&
                        chnroutes_value_node && YAML_SCALAR_NODE == chnroutes_value_node->type) {
                        const char *chnroutes_key = (const char *)chnroutes_key_node->data.scalar.value;
                        const char *chnroutes_value = (const char *)chnroutes_value_node->data.scalar.value;
                        if (0 == strcmp(chnroutes_key, "file-path")) {
                            strncpy(chnroutes_file_path, chnroutes_value, sizeof(chnroutes_file_path) - 1);
                        }
                    }
                }
                if (chnroutes_file_path[0] != '\0') {
                    chnroutes_enabled = 1;
                }
            }
        } else if (0 == strcmp (key, "smart-proxy"))
            res = hev_config_parse_smart_proxy (doc, node);

        if (res < 0)
            return -1;
    }

    if (tcp_buffer_size > TCP_SND_BUF)
        tcp_buffer_size = TCP_SND_BUF;

    min_task_stack_size = TASK_STACK_SIZE + tcp_buffer_size;
    if (task_stack_size < min_task_stack_size)
        task_stack_size = min_task_stack_size;

    return 0;
}

int
hev_config_init_from_file (const char *config_path)
{
    yaml_parser_t parser;
    yaml_document_t doc;
    FILE *fp;
    int res = -1;

    if (!yaml_parser_initialize (&parser))
        goto exit;

    fp = fopen (config_path, "r");
    if (!fp) {
        fprintf (stderr, "Open %s failed!\n", config_path);
        goto exit_free_parser;
    }

    yaml_parser_set_input_file (&parser, fp);
    if (!yaml_parser_load (&parser, &doc)) {
        fprintf (stderr, "Parse %s failed!\n", config_path);
        goto exit_close_fp;
    }

    res = hev_config_parse_doc (&doc);
    yaml_document_delete (&doc);

exit_close_fp:
    fclose (fp);
exit_free_parser:
    yaml_parser_delete (&parser);
exit:
    return res;
}

int
hev_config_init_from_str (const unsigned char *config_str,
                          unsigned int config_len)
{
    yaml_parser_t parser;
    yaml_document_t doc;
    int res = -1;

    if (!yaml_parser_initialize (&parser))
        goto exit;

    yaml_parser_set_input_string (&parser, config_str, config_len);
    if (!yaml_parser_load (&parser, &doc)) {
        fprintf (stderr, "Failed to parse config.");
        goto exit_free_parser;
    }

    res = hev_config_parse_doc (&doc);
    yaml_document_delete (&doc);

exit_free_parser:
    yaml_parser_delete (&parser);
exit:
    return res;
}

void
hev_config_fini (void)
{
}

const char *
hev_config_get_tunnel_name (void)
{
    if (!tun_name[0])
        return NULL;

    return tun_name;
}

unsigned int
hev_config_get_tunnel_mtu (void)
{
    return tun_mtu;
}

int
hev_config_get_tunnel_multi_queue (void)
{
    return multi_queue;
}

const char *
hev_config_get_tunnel_ipv4_address (void)
{
    if (!tun_ipv4_address[0])
        return NULL;

    return tun_ipv4_address;
}

const char *
hev_config_get_tunnel_ipv6_address (void)
{
    if (!tun_ipv6_address[0])
        return NULL;

    return tun_ipv6_address;
}

const char *
hev_config_get_tunnel_post_up_script (void)
{
    if (!tun_post_up_script[0])
        return NULL;

    return tun_post_up_script;
}

const char *
hev_config_get_tunnel_pre_down_script (void)
{
    if (!tun_pre_down_script[0])
        return NULL;

    return tun_pre_down_script;
}

HevConfigServer *
hev_config_get_socks5_tcp_server (void)
{
    return &srv_tcp;
}

HevConfigServer *
hev_config_get_socks5_udp_server (void)
{
    return &srv_udp;
}

int
hev_config_get_mapdns_address (void)
{
    return mapdns_address;
}

int
hev_config_get_mapdns_port (void)
{
    return mapdns_port;
}

int
hev_config_get_mapdns_network (void)
{
    return mapdns_network;
}

int
hev_config_get_mapdns_netmask (void)
{
    return mapdns_netmask;
}

int
hev_config_get_mapdns_cache_size (void)
{
    return mapdns_cache_size;
}

const char *
hev_config_get_dns_forwarder_virtual_ip4 (void)
{
    if (!fwd_virtual_ip4[0])
        return NULL;

    return fwd_virtual_ip4;
}

const char *
hev_config_get_dns_forwarder_virtual_ip6 (void)
{
    if (!fwd_virtual_ip6[0])
        return NULL;

    return fwd_virtual_ip6;
}

const char *
hev_config_get_dns_forwarder_target4 (void)
{
    if (!fwd_target4[0])
        return NULL;

    return fwd_target4;
}

const char *
hev_config_get_dns_forwarder_target6 (void)
{
    if (!fwd_target6[0])
        return NULL;

    return fwd_target6;
}

int
hev_config_get_misc_task_stack_size (void)
{
    return task_stack_size;
}

int
hev_config_get_misc_tcp_buffer_size (void)
{
    return tcp_buffer_size;
}

int
hev_config_get_misc_connect_timeout (void)
{
    return connect_timeout;
}

int
hev_config_get_misc_read_write_timeout (void)
{
    return read_write_timeout;
}

int
hev_config_get_misc_limit_nofile (void)
{
    return limit_nofile;
}

const char *
hev_config_get_misc_pid_file (void)
{
    if (!pid_file[0])
        return NULL;

    return pid_file;
}

const char *
hev_config_get_misc_log_file (void)
{
    if (!log_file[0])
        return "stderr";

    return log_file;
}

int
hev_config_get_misc_log_level (void)
{
    return log_level;
}

EXPORT_SYMBOL int
hev_config_get_chnroutes_enabled (void)
{
    return chnroutes_enabled;
}

EXPORT_SYMBOL const char *
hev_config_get_chnroutes_file_path (void)
{
    if (!chnroutes_file_path[0])
        return NULL;

    return chnroutes_file_path;
}

EXPORT_SYMBOL int
hev_config_get_smart_proxy_enabled (void)
{
    return smart_proxy_enabled;
}

EXPORT_SYMBOL unsigned int
hev_config_get_smart_proxy_timeout_ms (void)
{
    return smart_proxy_timeout_ms;
}

EXPORT_SYMBOL unsigned int
hev_config_get_smart_proxy_blocked_ip_expiry_minutes (void)
{
    return smart_proxy_blocked_ip_expiry_minutes;
}