/*
 ============================================================================
 Name        : hev-mapped-dns.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Mapped DNS
 ============================================================================
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

#include <hev-compiler.h>
#include <hev-memory-allocator.h>

#include "hev-logger.h"

#include "hev-mapped-dns.h"

static HevMappedDNS *singleton;

typedef struct _DNSHdr DNSHdr;

struct _DNSHdr
{
    uint16_t id;
    uint16_t fl;
    uint16_t qd;
    uint16_t an;
    uint16_t ns;
    uint16_t ar;
};

static const DNSHdr dnshdr_example = {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    .fl = 0x8081,
    .qd = 0x0100,
    .an = 0x0100,
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    .fl = 0x8180,
    .qd = 0x0001,
    .an = 0x0001,
#endif
    .id = 0x0,
    .ns = 0x0,
    .ar = 0x0
};//Forged DNS response header template

#pragma pack(1)
static struct {
    uint16_t name;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t len;
    uint32_t data;
} dns_answer_example = {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    .name = 0x0cc0,
    .type = 0x0100,
    .class = 0x0100,
    .ttl = 0x01000000,
    .len = 0x0400,
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    .name = 0xc00c,
    .type = 0x0001,
    .class = 0x0001,
    .ttl = 0x00000001,
    .len = 0x0004,
#endif
    .data= 0x0
};//4.1.3. Resource record format "dns ipv4 A answer"
#pragma pack()

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
struct _RfcDNSHeadFlag
{
    uint16_t ra		: 1;
    uint16_t z		: 3;
    uint16_t rcode	: 4;
    uint16_t qr		: 1;
    uint16_t opcode	: 4;
    uint16_t aa		: 1;
    uint16_t tc		: 1;
    uint16_t rd		: 1;
};//rfc1035 4.1.1. Header section format
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
struct _RfcDNSHeadFlag
{
    uint16_t qr		: 1;
    uint16_t opcode	: 4;
    uint16_t aa		: 1;
    uint16_t tc		: 1;
    uint16_t rd		: 1;
    uint16_t ra		: 1;
    uint16_t z		: 3;
    uint16_t rcode	: 4;
};//rfc1035 4.1.1. Header section format
#endif


#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
struct _IPv4Dec {
    uint8_t d;
    uint8_t c;
    uint8_t b;
    uint8_t a;
};
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
struct _IPv4Dec {
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint8_t d;
};
#endif


HevMappedDNS *
hev_mapped_dns_init (int net, int mask, int max)
{
    HevMappedDNS *self;
    self = malloc(sizeof(HevMappedDNS));
    if (!self)
        return NULL;
    memset(self, 0, sizeof(HevMappedDNS));
    int pool_size = (~mask >= max) ? max : ~mask;
    self->domain.buf = malloc(sizeof(RfcDomainNameLength) * pool_size);
    if (!(self->domain.buf)) {
        free(self);
        return NULL;
    }
    self->max = pool_size;
    memset(self->domain.buf, 0, (sizeof(RfcDomainNameLength) * pool_size));
    self->domain.hip = net;
    self->domain.tip = (self->domain.hip) + pool_size;
    LOG_I ("%p mapdns ip pools [map ipv4: %d] [memory byte: %d]", self->domain.buf, pool_size, (pool_size * sizeof(RfcDomainNameLength)));
    self->domain.head = self->domain.buf;
    self->domain.tail = (self->domain.buf) + pool_size;
    self->domain.read = self->domain.head;
    self->domain.write = self->domain.head;

hev_mapped_dns_cache_ttl(0xff);
    return self;
}
void
hev_mapped_dns_destroy(HevMappedDNS *self)
{
    memset((self->domain.buf), 0, sizeof(RfcDomainNameLength) * (self->max));
    free(self->domain.buf);
    memset(self, 0, sizeof(HevMappedDNS));
    free(self);
}

HevMappedDNS *
hev_mapped_dns_get (void)
{
    return singleton;
}

void
hev_mapped_dns_put (HevMappedDNS *self)
{
    singleton = self;
}

const char *
hev_mapped_dns_lookup (HevMappedDNS *self, int ip)
{
    if(ip >= (self->domain.hip) && ip <= (self->domain.tip)) {
        char *name = (char *)((self->domain.buf) + (ip - (self->domain.hip)));
        return name;
    }
    return NULL;
}

void
hev_mapped_dns_cache_ttl (int ttl)
{
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        ttl = htonl(ttl);
    #endif
    dns_answer_example.ttl = ttl;
}//dns.resp.ttl 

static int
hev_mapped_dns_block (HevMappedDNS *self, char *qname)
{
    //qname = dns.qry.name
    int ipv4;
    uint8_t *reject = (uint8_t *)&ipv4;
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        reject = &(reject[0]);
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        reject = &(reject[3]);
    #endif
    if(self->domain.write != self->domain.head)
        (self->domain.write)++;

    /*  ipv4 = x.x.x.255  *reject = 255 "Pointing to a location example"
     *  Some special address need to be skipped during allocation:
     *    rfc1812  4.2.2.11 Addres_ipsing (a) { 0, 0 } "skip address x.x.0.0"
     *    rfc1812  4.2.2.11 Addres_ipsing (c) { -1, -1 } "skip address x.x.255.255"
     *    rfc1812  4.2.2.11 Addres_ipsing (d) { <Network-prefix>, -1 } "skip address x.x.x.255"
     */
    while(1) {
        if(self->domain.write >= self->domain.head && self->domain.write <= self->domain.tail) {
        }//Quickly verify the pointer to ensure it is valid.
        else {
            self->domain.write = self->domain.head;
            LOG_I ("%p mapdns Jump to the first block in the circular buffer", (self->domain.buf));
        }

        ipv4 = ((self->domain.write) - (self->domain.head)) + (self->domain.hip);
        if(*reject == 0x0 || *reject == 0xff) {
            //skip address x.x.x.0 x.x.x.255
            (self->domain.write)++;
        }
        else {
            break;
        }
    }

    uint8_t labels;
    uint8_t names = 0;
    char *write = (char *) (self->domain.write);
    char *read = qname;
    while(1) {
        labels = (uint8_t)(*read);
        read++;
        if(labels > 63 || names == 0xff)//rfc1035 2.3.4:  1.labels 63 octets or less  2.names 255 octets or less
            break;
        memcpy(write, read, labels);
        names = names + labels + 1;
        read += labels;
        write += labels;
        if(*read == 0x00) {
            *write = 0x00;
            break;
        }
        else {
            *write = 0x2e;
        }
        write++;
    }
   write = (char *) (self->domain.write);
   struct  _IPv4Dec *dec = (struct  _IPv4Dec *)&ipv4;
   LOG_I ("%p mapped map index:%d [%d.%d.%d.%d -> %s]", (self->domain.write), (int)(self->domain.write - self->domain.head), dec->a, dec->b, dec->c, dec->d, write);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return htonl(ipv4);
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        return ipv4;
    #endif
    //Returns network bytes IPv4
}


int hev_mapped_dns_handle (HevMappedDNS *self, void *req, int qlen, void *res,
                       int slen)
{
    DNSHdr *qhdr = req;
    DNSHdr *shdr = res;
    RfcDNSHeadFlag *qflg = (RfcDNSHeadFlag *) &(qhdr->fl);
    if (slen < qlen)
         return -1;

    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if(qhdr->qd != 0x0100) {
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if(qhdr->qd != 0x0001) {
    #endif
            LOG_I ("%p mapdns dns.count.queries %d", qhdr, ntohs(qhdr->qd));
        }/*The RFC specifies that a single DNS packet can contain multiple `dns.count.queries`,
            but in practice, only one is typically used. If you see this warning in the logs, open an Issues window. */

    if(qflg->qr != 0b0 || qflg->opcode != 0b0) {
        LOG_I ("%p mapdns only accept standard query qr:%d opcode:%d", qflg, (qflg->qr), (qflg->opcode));
        return -1;//only accept standard query (QUERY)
    }

    if (qflg->tc != 0b0) {
        LOG_I ("%p mapdns only accept Message is not truncated tc:%d", qflg, (qflg->tc));
        return -1;//only accept Message is not truncated
    }

    memcpy (shdr, &dnshdr_example, sizeof(DNSHdr));//copy dns head flag example
    shdr->id = qhdr->id;

    char *qname = ((char *) qhdr) + sizeof(DNSHdr);
    char *sname = ((char *) shdr) + sizeof(DNSHdr);
    int qnamelen = strlen(qname) + 4 + 1;
    memcpy(sname, qname, qnamelen);
    /* 5:
     * dns.qry.name end 1byte
     * dns.qry.type 2byte
     * dns.qry.class 2byte
     */
    uint16_t *qry_class = (uint16_t *)(qname + qnamelen - 2);
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        if(*qry_class != 0x0100) {
    #elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        if(*qry_class != 0x0001) {
    #endif
            uint16_t *qry_type = (uint16_t *)(qname + qnamelen - 4);
            LOG_I ("%p mapdns reject Type:0x%x Class:0x%x  ID:0x%x", qhdr, ntohs(*qry_type), ntohs(*qry_class), ntohs(qhdr->id));
            return -1;
        }//Only accepts queries with Class:IN
    dns_answer_example.data = hev_mapped_dns_block(self, qname);

    char *answer = sname + qnamelen;
    memcpy(answer, &dns_answer_example, sizeof(dns_answer_example));
    return (qnamelen + sizeof(DNSHdr) + sizeof(dns_answer_example));
}
