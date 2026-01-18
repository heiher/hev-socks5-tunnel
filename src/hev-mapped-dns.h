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

typedef char RfcDomainNameLength[256];
/* rfc1035  2.3.4. Size limits "names 255 octets or less" */

typedef struct _RfcDNSHeadFlag RfcDNSHeadFlag;
/* rfc1035 4.1.1. Header section format */

struct _HevMappedDNS
{
    int use;
    int max;
    int net;
    int mask;// domain.buf block memory size = min(~mask, max) * sizeof(RfcDomainNameLength[256])
    struct {
        int hip;//First IP address
        int tip;//Last IP address
        RfcDomainNameLength *buf;
        RfcDomainNameLength *head;
        RfcDomainNameLength *tail;
        RfcDomainNameLength *read;
        RfcDomainNameLength *write;
    }domain;
};
/* _HevMappedDNS.doamin Circular buffer
 * A DNS request occupies one "RfcDomainNameLength".
 * Mapping methods:
 *   (QueryIP - domain.hip) = index
 *   domain->buf[index] = *DomainNameStr
*/

HevMappedDNS *hev_mapped_dns_init (int net, int mask, int max);
void hev_mapped_dns_destroy (HevMappedDNS *self);

HevMappedDNS *hev_mapped_dns_get (void);
void hev_mapped_dns_put (HevMappedDNS *self);

void hev_mapped_dns_cache_ttl (int ttl);

int hev_mapped_dns_handle (HevMappedDNS *self, void *req, int qlen, void *res,
                           int slen);
const char *hev_mapped_dns_lookup (HevMappedDNS *self, int ip);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_MAPPED_DNS_H__ */
