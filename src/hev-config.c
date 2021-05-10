/*
 ============================================================================
 Name        : hev-config.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2020 Everyone.
 Description : Config
 ============================================================================
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <yaml.h>

#include "hev-config.h"
#include "hev-config-const.h"

static char tun_name[64];
static unsigned int tun_mtu = 8192;

static char tun_ipv4_address[16];
static char tun_ipv4_gateway[16];
static unsigned int tun_ipv4_prefix;

static char tun_ipv6_address[64];
static char tun_ipv6_gateway[64];
static unsigned int tun_ipv6_prefix;

static struct sockaddr_in6 socks5_address;

static char log_file[1024];
static char log_level[16];
static char pid_file[1024];
static int limit_nofile = -2;

static int
address_to_sockaddr (const char *address, unsigned short port,
                     struct sockaddr_in6 *addr)
{
    __builtin_bzero (addr, sizeof (*addr));

    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons (port);
    if (inet_pton (AF_INET, address, &addr->sin6_addr.s6_addr[12]) == 1) {
        ((uint16_t *)&addr->sin6_addr)[5] = 0xffff;
    } else {
        if (inet_pton (AF_INET6, address, &addr->sin6_addr) != 1)
            return -1;
    }

    return 0;
}

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
        else if (0 == strcmp (key, "gateway"))
            strncpy (tun_ipv4_gateway, value, 16 - 1);
        else if (0 == strcmp (key, "prefix"))
            tun_ipv4_prefix = strtoul (value, NULL, 10);
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
        else if (0 == strcmp (key, "gateway"))
            strncpy (tun_ipv6_gateway, value, 64 - 1);
        else if (0 == strcmp (key, "prefix"))
            tun_ipv6_prefix = strtoul (value, NULL, 10);
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
        } else {
            if (0 == strcmp (key, "ipv4"))
                hev_config_parse_tunnel_ipv4 (doc, node);
            else if (0 == strcmp (key, "ipv6"))
                hev_config_parse_tunnel_ipv6 (doc, node);
        }
    }

    if (!tun_ipv4_address[0] && !tun_ipv6_address[0]) {
        fprintf (stderr, "Can't found tunnel.ipv4/6.address!\n");
        return -1;
    }

    if (!tun_ipv4_gateway[0] && !tun_ipv6_gateway[0]) {
        fprintf (stderr, "Can't found tunnel.ipv4/6.gateway!\n");
        return -1;
    }

    if (!tun_ipv4_prefix && !tun_ipv6_prefix) {
        fprintf (stderr, "Can't found tunnel.ipv4/6.prefix!\n");
        return -1;
    }

    return 0;
}

static int
hev_config_parse_socks5 (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    int port = 0;
    const char *addr = NULL;

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
            port = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "address"))
            addr = value;
    }

    if (!port) {
        fprintf (stderr, "Can't found socks5.port!\n");
        return -1;
    }

    if (!addr) {
        fprintf (stderr, "Can't found socks5.address!\n");
        return -1;
    }

    return address_to_sockaddr (addr, port, &socks5_address);
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

        if (0 == strcmp (key, "pid-file"))
            strncpy (pid_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-file"))
            strncpy (log_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-level"))
            strncpy (log_level, value, 16 - 1);
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
        else if (0 == strcmp (key, "misc"))
            res = hev_config_parse_misc (doc, node);

        if (res < 0)
            return -1;
    }

    return 0;
}

int
hev_config_init (const char *config_path)
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

const char *
hev_config_get_tunnel_ipv4_address (void)
{
    if (!tun_ipv4_address[0])
        return NULL;

    return tun_ipv4_address;
}

const char *
hev_config_get_tunnel_ipv4_gateway (void)
{
    if (!tun_ipv4_gateway[0])
        return NULL;

    return tun_ipv4_gateway;
}

unsigned int
hev_config_get_tunnel_ipv4_prefix (void)
{
    return tun_ipv4_prefix;
}

const char *
hev_config_get_tunnel_ipv6_address (void)
{
    if (!tun_ipv6_address[0])
        return NULL;

    return tun_ipv6_address;
}

const char *
hev_config_get_tunnel_ipv6_gateway (void)
{
    if (!tun_ipv6_gateway[0])
        return NULL;

    return tun_ipv6_gateway;
}

unsigned int
hev_config_get_tunnel_ipv6_prefix (void)
{
    return tun_ipv6_prefix;
}

struct sockaddr *
hev_config_get_socks5_address (socklen_t *addr_len)
{
    *addr_len = sizeof (socks5_address);
    return (struct sockaddr *)&socks5_address;
}

const char *
hev_config_get_misc_pid_file (void)
{
    if (!pid_file[0])
        return NULL;

    return pid_file;
}

int
hev_config_get_misc_limit_nofile (void)
{
    return limit_nofile;
}

const char *
hev_config_get_misc_log_file (void)
{
    if (!log_file[0])
        return NULL;
    if (0 == strcmp (log_file, "null"))
        return NULL;

    return log_file;
}

const char *
hev_config_get_misc_log_level (void)
{
    if (!log_level[0])
        return "warn";

    return log_level;
}
