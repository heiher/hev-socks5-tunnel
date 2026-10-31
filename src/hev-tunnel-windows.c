/*
 ============================================================================
 Name        : hev-tunnel-windows.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2025 hev
 Description : Tunnel on Windows
 ============================================================================
 */

#ifdef _WIN32

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <hev-sys.h>
#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-wintun.h"

#include "hev-tunnel.h"

static HevWinTun *wintun;
static HevWinTunAdapter *adapter;
static HevWinTunSession *session;
static char tun_name[IFNAMSIZ];

int
hev_tunnel_open (const char *name, int multi_queue, const char *guid)
{
    wintun = hev_wintun_open ();
    if (!wintun)
        return -1;

    adapter = hev_wintun_adapter_create (name, guid);
    if (!adapter)
        goto free;

    strncpy (tun_name, name, IFNAMSIZ - 1);
    return 0;

free:
    hev_wintun_close (wintun);
    return -1;
}

void
hev_tunnel_close (int fd)
{
    if (session)
        hev_wintun_session_stop (session);
    hev_wintun_adapter_close (adapter);
    hev_wintun_close (wintun);
}

void
hev_tunnel_set_offload (int fd, int offload)
{
}

int
hev_tunnel_get_offload (void)
{
    return 0;
}

int
hev_tunnel_set_mtu (int mtu)
{
    return hev_wintun_adapter_set_mtu (adapter, mtu);
}

int
hev_tunnel_set_state (int state)
{
    if (state) {
        session = hev_wintun_session_start (adapter);
        if (!session)
            return -1;
    } else {
        if (session) {
            hev_wintun_session_stop (session);
            session = NULL;
        }
    }

    return 0;
}

int
hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix)
{
    char ipv4[4];
    int res;

    res = inet_pton (AF_INET, addr, ipv4);
    if (!res)
        return -1;

    return hev_wintun_adapter_set_ipv4 (adapter, ipv4, prefix);
}

int
hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix)
{
    char ipv6[16];
    int res;

    res = inet_pton (AF_INET6, addr, ipv6);
    if (!res)
        return -1;

    return hev_wintun_adapter_set_ipv6 (adapter, ipv6, prefix);
}

const char *
hev_tunnel_get_name (void)
{
    return tun_name;
}

const char *
hev_tunnel_get_index (void)
{
    static char tun_index[16];
    int index;

    index = hev_wintun_adapter_get_index (adapter);
    if (index < 0)
        return NULL;

    snprintf (tun_index, sizeof (tun_index), "%d", index);
    return tun_index;
}

int
hev_tunnel_add_task (int fd, HevTask *task)
{
    void *handle = hev_wintun_session_get_read_wait_event (session);
    return hev_task_add_whandle (task, handle);
}

void
hev_tunnel_del_task (int fd, HevTask *task)
{
    void *handle = hev_wintun_session_get_read_wait_event (session);
    hev_task_del_whandle (task, handle);
}

struct pbuf *
hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder, void *yielder_data)
{
    struct pbuf *buf;
    void *packet;
    int spin = 0;
    int size;

retry:
    packet = hev_wintun_session_receive (session, &size);
    if (!packet) {
        if (hev_wintun_get_last_error () == HEV_WINTUN_EAGAIN) {
            if (spin++ < 1000) {
                hev_task_yield (HEV_TASK_YIELD);
                goto retry;
            }

            /* The wait of a handle is one-shot, arm it before each sleep. */
            hev_tunnel_add_task (fd, hev_task_self ());
            if (yielder) {
                if (yielder (HEV_TASK_WAITIO, yielder_data))
                    return NULL;
            } else {
                hev_task_yield (HEV_TASK_WAITIO);
            }
            goto retry;
        } else {
            return NULL;
        }
    }

    /* Copy and release at once: pbufs pointing into the ring are held by lwip
     * until consumed and, once the ring is full, no packet is received. */
    buf = pbuf_alloc (PBUF_RAW, size, PBUF_RAM);
    if (buf)
        pbuf_take (buf, packet, size);
    hev_wintun_session_release (session, packet);

    return buf;
}

ssize_t
hev_tunnel_write (int fd, struct pbuf *buf)
{
    struct pbuf *p = buf;
    void *packet;
    size_t size;

    packet = hev_wintun_session_allocate (session, p->tot_len);
    if (!packet) {
        /* The send ring is full, the packet is dropped. */
        if (hev_wintun_get_last_error () == HEV_WINTUN_ENOBUFS)
            errno = ENOBUFS;
        return -1;
    }

    for (size = 0; p; p = p->next) {
        memcpy (packet + size, p->payload, p->len);
        size += p->len;
    }

    hev_wintun_session_send (session, packet);
    return size;
}

#endif /* _WIN32 */
