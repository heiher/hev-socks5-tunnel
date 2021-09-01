/*
 ============================================================================
 Name        : hev-socks5-tunnel.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2021 hev
 Description : Socks5 Tunnel
 ============================================================================
 */

#include <signal.h>
#include <unistd.h>

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/nd6.h>
#include <lwip/pbuf.h>
#include <lwip/netif.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_frag.h>
#include <lwip/ip6_frag.h>
#include <lwip/priv/tcp_priv.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-pipe.h>
#include <hev-task-mutex.h>
#include <hev-task-system.h>
#include <hev-memory-allocator.h>

#include "hev-list.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-compiler.h"
#include "hev-tunnel-linux.h"
#include "hev-socks5-session-tcp.h"
#include "hev-socks5-session-udp.h"

#include "hev-socks5-tunnel.h"

static int run;
static int tun_fd;
static int event_fds[2];

static struct netif netif;
static struct tcp_pcb *tcp;
static struct udp_pcb *udp;

static HevTaskMutex mutex;
static HevTask *task_event;
static HevTask *task_lwip_io;
static HevTask *task_lwip_timer;
static HevList session_set;

static int tunnel_init (void);
static int gateway_init (void);
static void sigint_handler (int signum);
static void event_task_entry (void *data);
static void lwip_io_task_entry (void *data);
static void lwip_timer_task_entry (void *data);
static err_t tcp_accept_handler (void *arg, struct tcp_pcb *pcb, err_t err);
static void udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port);

int
hev_socks5_tunnel_init (int tunfd)
{
    LOG_D ("socks5 tunnel init");

    if (hev_task_system_init () < 0) {
        LOG_E ("socks5 tunnel task system");
        goto exit;
    }

    if (tunfd < 0) {
        if (tunnel_init () < 0)
            goto exit_free_sys;
    } else {
        tun_fd = tunfd;
    }

    if (hev_task_io_pipe_pipe (event_fds) < 0) {
        LOG_E ("socks5 tunnel pipe");
        goto exit_close_tun;
    }

    if (gateway_init () < 0)
        goto exit_close_event;

    hev_task_mutex_init (&mutex);

    task_event = hev_task_new (-1);
    if (!task_event) {
        LOG_E ("socks5 tunnel task event");
        goto exit_free_gateway;
    }

    if (hev_task_add_fd (task_event, event_fds[0], POLLIN) < 0) {
        LOG_E ("socks5 tunnel add eventfd");
        goto exit_free_task_event;
    }

    task_lwip_io = hev_task_new (-1);
    if (!task_lwip_io) {
        LOG_E ("socks5 tunnel task lwip");
        goto exit_free_task_event;
    }
    hev_task_set_priority (task_lwip_io, HEV_TASK_PRIORITY_REALTIME);

    if (hev_task_add_fd (task_lwip_io, tun_fd, POLLIN | POLLOUT) < 0) {
        LOG_E ("socks5 tunnel add tunfd");
        goto exit_free_task_lwip_io;
    }

    task_lwip_timer = hev_task_new (-1);
    if (!task_lwip_timer) {
        LOG_E ("socks5 tunnel task timer");
        goto exit_free_task_lwip_io;
    }
    hev_task_set_priority (task_lwip_timer, HEV_TASK_PRIORITY_REALTIME);

    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOG_E ("socks5 tunnel sigpipe");
        goto exit_free_task_lwip_timer;
    }

    if (signal (SIGINT, sigint_handler) == SIG_ERR) {
        LOG_E ("socks5 tunnel sigint");
        goto exit_free_task_lwip_timer;
    }

    return 0;

exit_free_task_lwip_timer:
    hev_task_unref (task_lwip_timer);
exit_free_task_lwip_io:
    hev_task_unref (task_lwip_io);
exit_free_task_event:
    hev_task_unref (task_event);
exit_free_gateway:
    udp_remove (udp);
    tcp_close (tcp);
    netif_remove (&netif);
exit_close_event:
    close (event_fds[0]);
    close (event_fds[1]);
exit_close_tun:
    close (tun_fd);
exit_free_sys:
    hev_task_system_fini ();
exit:
    return -1;
}

void
hev_socks5_tunnel_fini (void)
{
    LOG_D ("socks5 tunnel fini");

    hev_task_unref (task_lwip_timer);
    hev_task_unref (task_lwip_io);
    hev_task_unref (task_event);

    udp_remove (udp);
    tcp_close (tcp);
    netif_remove (&netif);

    close (event_fds[0]);
    close (event_fds[1]);
    close (tun_fd);

    hev_task_system_fini ();
}

int
hev_socks5_tunnel_run (void)
{
    LOG_D ("socks5 tunnel run");

    task_event = hev_task_ref (task_event);
    hev_task_run (task_event, event_task_entry, NULL);
    task_lwip_io = hev_task_ref (task_lwip_io);
    hev_task_run (task_lwip_io, lwip_io_task_entry, NULL);
    task_lwip_timer = hev_task_ref (task_lwip_timer);
    hev_task_run (task_lwip_timer, lwip_timer_task_entry, NULL);

    run = 1;
    hev_task_system_run ();

    return 0;
}

void
hev_socks5_tunnel_stop (void)
{
    int val;

    LOG_D ("socks5 tunnel stop");

    if (event_fds[1] == -1)
        return;

    if (write (event_fds[1], &val, sizeof (val)) == -1)
        LOG_E ("socks5 tunnel write event");
}

static int
tunnel_init (void)
{
    HevTunnelLinux *tun;
    const char *name, *ipv4, *ipv6;
    unsigned int mtu, ipv4_prefix, ipv6_prefix;

    name = hev_config_get_tunnel_name ();
    mtu = hev_config_get_tunnel_mtu ();
    ipv4 = hev_config_get_tunnel_ipv4_address ();
    ipv4_prefix = hev_config_get_tunnel_ipv4_prefix ();
    ipv6 = hev_config_get_tunnel_ipv6_address ();
    ipv6_prefix = hev_config_get_tunnel_ipv6_prefix ();

    tun = hev_tunnel_linux_new (name);
    if (!tun) {
        LOG_E ("socks5 tunnel new");
        goto exit;
    }

    if (hev_tunnel_linux_set_mtu (tun, mtu) < 0) {
        LOG_E ("socks5 tunnel mtu");
        goto exit_free;
    }

    if (ipv4 && (hev_tunnel_linux_set_ipv4 (tun, ipv4, ipv4_prefix) < 0)) {
        LOG_E ("socks5 tunnel ipv4");
        goto exit_free;
    }

    if (ipv6 && (hev_tunnel_linux_set_ipv6 (tun, ipv6, ipv6_prefix) < 0)) {
        LOG_E ("socks5 tunnel ipv6");
        goto exit_free;
    }

    if (hev_tunnel_linux_set_state (tun, 1) < 0) {
        LOG_E ("socks5 tunnel state");
        goto exit_free;
    }

    tun_fd = dup (hev_tunnel_linux_get_fd (tun));
    if (tun_fd < 0) {
        LOG_E ("socks5 tunnel dup fd");
        goto exit_free;
    }

    hev_tunnel_linux_destroy (tun);
    return 0;

exit_free:
    hev_tunnel_linux_destroy (tun);
exit:
    return -1;
}

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    hev_task_yield (type);

    return run ? 0 : -1;
}

static err_t
netif_output_handler (struct netif *netif, struct pbuf *p)
{
    ssize_t s;

    if (p->next) {
        struct iovec iov[512];
        int i;

        for (i = 0; (i < 512) && p; p = p->next) {
            iov[i].iov_base = p->payload;
            iov[i].iov_len = p->len;
            i++;
        }

        s = hev_task_io_writev (tun_fd, iov, i, task_io_yielder, NULL);
    } else {
        s = hev_task_io_write (tun_fd, p->payload, p->len, task_io_yielder,
                               NULL);
    }

    if (s <= 0) {
        LOG_W ("socks5 tunnel write");
        return ERR_IF;
    }

    return ERR_OK;
}

static err_t
netif_output_v4_handler (struct netif *netif, struct pbuf *p,
                         const ip4_addr_t *ipaddr)
{
    return netif_output_handler (netif, p);
}

static err_t
netif_output_v6_handler (struct netif *netif, struct pbuf *p,
                         const ip6_addr_t *ipaddr)
{
    return netif_output_handler (netif, p);
}

static err_t
netif_init_handler (struct netif *netif)
{
    netif->name[0] = 'v';
    netif->name[1] = 'p';

    netif->output = netif_output_v4_handler;
    netif->output_ip6 = netif_output_v6_handler;

    return ERR_OK;
}

static int
gateway_init (void)
{
    const char *ipv4, *ipv6;

    ipv4 = hev_config_get_tunnel_ipv4_gateway ();
    ipv6 = hev_config_get_tunnel_ipv6_gateway ();

    if (!netif_add_noaddr (&netif, NULL, netif_init_handler, ip_input)) {
        LOG_E ("socks5 tunnel lwip netif");
        goto exit;
    }

    if (ipv4) {
        unsigned int prefix = hev_config_get_tunnel_ipv4_prefix ();
        ip4_addr_t addr, mask, gw;

        if (!ip4addr_aton (ipv4, &addr)) {
            LOG_E ("socks5 tunnel lwip ipv4");
            goto exit_free_netif;
        }
        mask.addr = htonl (((unsigned int)(-1)) << (32 - prefix));
        ip4_addr_set_any (&gw);
        netif_set_addr (&netif, &addr, &mask, &gw);
    }

    if (ipv6) {
        ip6_addr_t addr;

        if (!ip6addr_aton (ipv6, &addr)) {
            LOG_E ("socks5 tunnel lwip ipv6");
            goto exit_free_netif;
        }
        netif_add_ip6_address (&netif, &addr, NULL);
    }

    netif_set_up (&netif);
    netif_set_link_up (&netif);
    netif_set_default (&netif);
    netif_set_flags (&netif, NETIF_FLAG_PRETEND_TCP);

    tcp = tcp_new_ip_type (IPADDR_TYPE_ANY);
    if (!tcp) {
        LOG_E ("socks5 tunnel tcp");
        goto exit_free_netif;
    }

    tcp_bind_netif (tcp, &netif);
    if (tcp_bind (tcp, NULL, 0) != ERR_OK) {
        LOG_E ("socks5 tunnel tcp bind");
        goto exit_free_tcp;
    }

    if (!(tcp = tcp_listen (tcp))) {
        LOG_E ("socks5 tunnel tcp listen");
        goto exit_free_tcp;
    }

    tcp_accept (tcp, tcp_accept_handler);

    udp = udp_new_ip_type (IPADDR_TYPE_ANY);
    if (!udp) {
        LOG_E ("socks5 tunnel udp");
        goto exit_free_tcp;
    }

    udp_bind_netif (udp, &netif);
    if (udp_bind (udp, NULL, 0) != ERR_OK) {
        LOG_E ("socks5 tunnel bind");
        goto exit_free_udp;
    }

    udp_recv (udp, udp_recv_handler, NULL);

    return 0;

exit_free_udp:
    udp_remove (udp);
exit_free_tcp:
    tcp_close (tcp);
exit_free_netif:
    netif_remove (&netif);
exit:
    return -1;
}

static void
sigint_handler (int signum)
{
    hev_socks5_tunnel_stop ();
}

static void
event_task_entry (void *data)
{
    HevListNode *node;
    int val;

    LOG_D ("socks5 tunnel event task run");

    hev_task_io_read (event_fds[0], &val, sizeof (val), NULL, NULL);

    run = 0;
    hev_task_wakeup (task_lwip_io);
    hev_task_wakeup (task_lwip_timer);

    node = hev_list_first (&session_set);
    for (; node; node = hev_list_node_next (node)) {
        HevSocks5SessionData *sd;

        sd = container_of (node, HevSocks5SessionData, node);
        hev_socks5_session_terminate (sd->self);
    }
}

static void
lwip_io_task_entry (void *data)
{
    const unsigned int mtu = hev_config_get_tunnel_mtu ();

    LOG_D ("socks5 tunnel lwip task run");

    for (; run;) {
        struct pbuf *buf;
        ssize_t s;

        hev_task_mutex_lock (&mutex);
        buf = pbuf_alloc (PBUF_RAW, mtu, PBUF_RAM);
        hev_task_mutex_unlock (&mutex);
        if (!buf) {
            LOG_E ("socks5 tunnel alloc");
            hev_task_sleep (100);
            continue;
        }

        if (buf->next) {
            struct iovec iov[512];
            struct pbuf *p;
            int i;

            for (i = 0, p = buf; (i < 512) && p; p = p->next) {
                iov[i].iov_base = p->payload;
                iov[i].iov_len = p->len;
                i++;
            }

            s = hev_task_io_readv (tun_fd, iov, i, task_io_yielder, NULL);
        } else {
            s = hev_task_io_read (tun_fd, buf->payload, buf->len,
                                  task_io_yielder, NULL);
        }

        if (s <= 0) {
            LOG_E ("socks5 tunnel read");
            pbuf_free (buf);
            continue;
        }

        hev_task_mutex_lock (&mutex);
        if (netif.input (buf, &netif) != ERR_OK)
            pbuf_free (buf);
        hev_task_mutex_unlock (&mutex);
    }
}

static void
lwip_timer_task_entry (void *data)
{
    unsigned int i;

    LOG_D ("socks5 tunnel timer task run");

    for (i = 1;; i++) {
        hev_task_sleep (TCP_TMR_INTERVAL);
        if (!run)
            break;

        hev_task_mutex_lock (&mutex);
        tcp_tmr ();

        if ((i & 3) == 0) {
#if IP_REASSEMBLY
            ip_reass_tmr ();
#endif
#if LWIP_IPV6
            nd6_tmr ();
#if LWIP_IPV6_REASS
            ip6_reass_tmr ();
#endif
#endif
        }
        hev_task_mutex_unlock (&mutex);
    }
}

static void
hev_socks5_session_task_entry (void *data)
{
    HevSocks5Session *s = data;

    hev_socks5_session_run (s);

    hev_list_del (&session_set, hev_socks5_session_get_node (s));
    hev_object_unref (HEV_OBJECT (s));
}

static err_t
tcp_accept_handler (void *arg, struct tcp_pcb *pcb, err_t err)
{
    HevSocks5SessionTCP *tcp;
    HevListNode *node;
    int stack_size;
    HevTask *task;

    if (err != ERR_OK)
        return err;

    tcp = hev_socks5_session_tcp_new (pcb, &mutex);
    if (!tcp)
        return ERR_MEM;

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        hev_object_unref (HEV_OBJECT (tcp));
        return ERR_ABRT;
    }

    hev_socks5_session_set_task (HEV_SOCKS5_SESSION (tcp), task);
    node = hev_socks5_session_get_node (HEV_SOCKS5_SESSION (tcp));
    hev_list_add_tail (&session_set, node);
    hev_task_run (task, hev_socks5_session_task_entry, tcp);

    return ERR_OK;
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5SessionUDP *udp;
    HevListNode *node;
    int stack_size;
    HevTask *task;

    udp = hev_socks5_session_udp_new (pcb, &mutex);
    if (!udp) {
        udp_remove (pcb);
        return;
    }

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        hev_object_unref (HEV_OBJECT (udp));
        return;
    }

    hev_socks5_session_set_task (HEV_SOCKS5_SESSION (udp), task);
    node = hev_socks5_session_get_node (HEV_SOCKS5_SESSION (udp));
    hev_list_add_tail (&session_set, node);
    hev_task_run (task, hev_socks5_session_task_entry, udp);
}
