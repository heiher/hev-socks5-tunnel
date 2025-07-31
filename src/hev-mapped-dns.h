/*
 ============================================================================
 Name        : hev-mapped-dns.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Mapped DNS
 ============================================================================
 */

#ifndef __HEV_MAPPED_DNS_H__
#define __HEV_MAPPED_DNS_H__

#include <hev-list.h>
#include <hev-rbtree.h>
#include <hev-object.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEV_MAPPED_DNS(p) ((HevMappedDNS *)p)
#define HEV_MAPPED_DNS_CLASS(p) ((HevMappedDNSClass *)p)
#define HEV_MAPPED_DNS_TYPE (hev_mapped_dns_class ())

typedef struct _HevMappedDNS HevMappedDNS;
typedef struct _HevMappedDNSClass HevMappedDNSClass;
typedef struct _HevMappedDNSNode HevMappedDNSNode;

struct _HevMappedDNS
{
    HevObject base;

    int use;
    int max;
    int net;
    int mask;

    HevList list;
    HevRBTree tree;
    HevMappedDNSNode *records[0];
};

struct _HevMappedDNSClass
{
    HevObjectClass base;
};

HevObjectClass *hev_mapped_dns_class (void);

int hev_mapped_dns_construct (HevMappedDNS *self, int net, int mask, int max);

HevMappedDNS *hev_mapped_dns_new (int net, int mask, int max);

HevMappedDNS *hev_mapped_dns_get (void);
void hev_mapped_dns_put (HevMappedDNS *self);

int hev_mapped_dns_handle (HevMappedDNS *self, void *req, int qlen, void *res,
                           int slen);
const char *hev_mapped_dns_lookup (HevMappedDNS *self, int ip);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_MAPPED_DNS_H__ */
