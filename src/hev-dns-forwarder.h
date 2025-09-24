/*
 ============================================================================
 Name        : hev-dns-forwarder.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : DNS Forwarder
 ============================================================================
 */

#ifndef __HEV_DNS_FORWARDER_H__
#define __HEV_DNS_FORWARDER_H__

#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <hev-task-mutex.h>

void hev_dns_forwarder_run (struct udp_pcb *pcb, HevTaskMutex *mutex,
                            struct pbuf *p, const ip_addr_t *addr, u16_t port,
                            int family);

#endif /* __HEV_DNS_FORWARDER_H__ */
