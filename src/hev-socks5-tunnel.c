/*
 ============================================================================
 Name        : hev-socks5-tunnel.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2025 hev
 Description : Socks5 Tunnel
 ============================================================================
 */

#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/nd6.h>
#include <lwip/netif.h>
#include <lwip/ip4_frag.h>
#include <lwip/ip6_frag.h>
#include <lwip/priv/tcp_priv.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-mutex.h>
#include <hev-task-system.h>
#include <hev-memory-allocator.h>

#include "hev-exec.h"
#include "hev-list.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-tunnel.h"
#include "hev-compiler.h"
#include "hev-mapped-dns.h"
#include "hev-config-const.h"
#include "hev-socks5-session-tcp.h"
#include "hev-socks5-session-udp.h"

#include "hev-socks5-tunnel.h"

static int run;
static int tun_fd = -1;
static int tun_fd_local;
static int event_fds[2] = { -1, -1 };

static size_t stat_tx_packets;
static size_t stat_rx_packets;
static size_t stat_tx_bytes;
static size_t stat_rx_bytes;

static struct netif netif;
static struct tcp_pcb *tcp;
static struct udp_pcb *udp;

static HevTaskMutex mutex;
static HevTask *task_event;
static HevTask *task_lwip_io;
static HevTask *task_lwip_timer;
static HevList session_set;

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

    s = hev_tunnel_write (tun_fd, p);
    if (s <= 0) {
        if (errno == EAGAIN)
            return ERR_WOULDBLOCK;
        LOG_W ("socks5 tunnel write");
        return ERR_IF;
    }

    stat_rx_packets++;
    stat_rx_bytes += s;

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
    netif->output = netif_output_v4_handler;
    netif->output_ip6 = netif_output_v6_handler;

    return ERR_OK;
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

    if (!run)
        return ERR_RST;

    tcp = hev_socks5_session_tcp_new (pcb, &mutex);
    if (!tcp)
        return ERR_MEM;

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        hev_object_unref (HEV_OBJECT (tcp));
        return ERR_MEM;
    }

    hev_socks5_session_set_task (HEV_SOCKS5_SESSION (tcp), task);
    node = hev_socks5_session_get_node (HEV_SOCKS5_SESSION (tcp));
    hev_list_add_tail (&session_set, node);
    hev_task_run (task, hev_socks5_session_task_entry, tcp);
    hev_task_wakeup (task_lwip_timer);

    return ERR_OK;
}

static void
dns_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevMappedDNS *dns = arg;
    struct pbuf *b;
    int res;

    LOG_D ("%p mapped dns handle", dns);

    b = pbuf_alloc (PBUF_TRANSPORT, UDP_BUF_SIZE, PBUF_RAM);
    if (!b)
        goto exit;

    res = hev_mapped_dns_handle (dns, p->payload, p->len, b->payload, b->len);
    if (res < 0)
        goto free;

    b->len = res;
    b->tot_len = res;
    udp_sendfrom (pcb, b, &pcb->local_ip, pcb->local_port);

free:
    pbuf_free (b);
exit:
    pbuf_free (p);
    udp_recv (pcb, NULL, NULL);
    udp_remove (pcb);
}

static void
udp_recv_handler (void *arg, struct udp_pcb *pcb, struct pbuf *p,
                  const ip_addr_t *addr, u16_t port)
{
    HevSocks5SessionUDP *udp;
    HevListNode *node;
    HevMappedDNS *dns;
    int stack_size;
    HevTask *task;

    if (!run) {
        udp_remove (pcb);
        return;
    }

    dns = hev_mapped_dns_get ();
    if (dns && addr->type == IPADDR_TYPE_V4) {
        int faddr = hev_config_get_mapdns_address ();
        int fport = hev_config_get_mapdns_port ();
        if (fport == port && faddr == ip_2_ip4 (addr)->addr) {
            udp_recv (pcb, dns_recv_handler, dns);
            return;
        }
    }

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
    hev_task_wakeup (task_lwip_timer);
}

static void
event_task_entry (void *data)
{
    HevListNode *node;
    int val;

    LOG_D ("socks5 tunnel event task run");

    hev_task_add_fd (task_event, event_fds[0], POLLIN);

    hev_task_io_read (event_fds[0], &val, sizeof (val), NULL, NULL);

    run = 0;
    node = hev_list_first (&session_set);
    for (; node; node = hev_list_node_next (node)) {
        HevSocks5SessionData *sd;

        sd = container_of (node, HevSocks5SessionData, node);
        hev_socks5_session_terminate (sd->self);
    }

    hev_task_join (task_lwip_io);
    hev_task_join (task_lwip_timer);
    hev_task_del_fd (task_event, event_fds[0]);
}

static void
lwip_io_task_entry (void *data)
{
    const unsigned int mtu = hev_config_get_tunnel_mtu ();

    LOG_D ("socks5 tunnel lwip task run");

    hev_tunnel_add_task (tun_fd, task_lwip_io);

    for (; run;) {
        struct pbuf *buf;

        buf = hev_tunnel_read (tun_fd, mtu, task_io_yielder, NULL);
        if (!buf)
            continue;

        stat_tx_packets++;
        stat_tx_bytes += buf->tot_len;

        hev_task_mutex_lock (&mutex);
        if (netif.input (buf, &netif) != ERR_OK)
            pbuf_free (buf);
        hev_task_mutex_unlock (&mutex);
    }

    hev_tunnel_del_task (tun_fd, task_lwip_io);
}

static void
lwip_timer_task_entry (void *data)
{
    unsigned int i;

    LOG_D ("socks5 tunnel timer task run");

    for (i = 1; run; i++) {
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

        if (hev_list_first (&session_set))
            hev_task_sleep (TCP_TMR_INTERVAL);
        else
            hev_task_yield (HEV_TASK_WAITIO);
    }
}

static int
tunnel_init (int extern_tun_fd)
{
    const char *script_path, *name, *ipv4, *ipv6;
    int multi_queue, res;
    unsigned int mtu;

    if (extern_tun_fd >= 0) {
        int nonblock = 1;

        res = ioctl (extern_tun_fd, FIONBIO, (char *)&nonblock);
        if (res < 0) {
            LOG_E ("socks5 tunnel non-blocking");
            return -1;
        }

        tun_fd = extern_tun_fd;
        return 0;
    }

    name = hev_config_get_tunnel_name ();
    multi_queue = hev_config_get_tunnel_multi_queue ();
    tun_fd = hev_tunnel_open (name, multi_queue);
    if (tun_fd < 0) {
        LOG_E ("socks5 tunnel open (%s)", strerror (errno));
        return -1;
    }

    mtu = hev_config_get_tunnel_mtu ();
    res = hev_tunnel_set_mtu (mtu);
    if (res < 0) {
        LOG_E ("socks5 tunnel mtu");
        return -1;
    }

    ipv4 = hev_config_get_tunnel_ipv4_address ();
    if (ipv4) {
        res = hev_tunnel_set_ipv4 (ipv4, 32);
        if (res < 0) {
            LOG_E ("socks5 tunnel ipv4");
            return -1;
        }
    }

    ipv6 = hev_config_get_tunnel_ipv6_address ();
    if (ipv6) {
        res = hev_tunnel_set_ipv6 (ipv6, 128);
        if (res < 0) {
            LOG_E ("socks5 tunnel ipv6");
            return -1;
        }
    }

    res = hev_tunnel_set_state (1);
    if (res < 0) {
        LOG_E ("socks5 tunnel state");
        return -1;
    }

    script_path = hev_config_get_tunnel_post_up_script ();
    if (script_path)
        hev_exec_run (script_path, hev_tunnel_get_name (),
                      hev_tunnel_get_index (), 0);

    tun_fd_local = 1;
    return 0;
}

static void
tunnel_fini (void)
{
    const char *script_path;

    if (!tun_fd_local)
        return;

    script_path = hev_config_get_tunnel_pre_down_script ();
    if (script_path)
        hev_exec_run (script_path, hev_tunnel_get_name (),
                      hev_tunnel_get_index (), 1);

    hev_tunnel_close (tun_fd);
    tun_fd_local = 0;
    tun_fd = -1;
}

static int
gateway_init (void)
{
    ip4_addr_t addr4, mask, gw;
    ip6_addr_t addr6;

    netif_add_noaddr (&netif, NULL, netif_init_handler, ip_input);

    ip4_addr_set_loopback (&addr4);
    ip4_addr_set_any (&mask);
    ip4_addr_set_any (&gw);
    netif_set_addr (&netif, &addr4, &mask, &gw);

    ip6_addr_set_loopback (&addr6);
    netif_add_ip6_address (&netif, &addr6, NULL);

    netif_set_up (&netif);
    netif_set_link_up (&netif);
    netif_set_default (&netif);
    netif_set_flags (&netif, NETIF_FLAG_PRETEND_TCP);

    tcp = tcp_new_ip_type (IPADDR_TYPE_ANY);
    tcp_bind_netif (tcp, &netif);
    tcp_bind (tcp, NULL, 0);
    tcp = tcp_listen (tcp);
    tcp_accept (tcp, tcp_accept_handler);

    udp = udp_new_ip_type (IPADDR_TYPE_ANY);
    udp_bind_netif (udp, &netif);
    udp_bind (udp, NULL, 0);
    udp_recv (udp, udp_recv_handler, NULL);

    return 0;
}

static void
gateway_fini (void)
{
    udp_remove (udp);
    tcp_close (tcp);
    netif_remove (&netif);
}

static int
event_task_init (void)
{
    int nonblock = 1;
    int res;

    res = socketpair (PF_LOCAL, SOCK_STREAM, 0, event_fds);
    if (res < 0) {
        LOG_E ("socks5 tunnel event");
        return -1;
    }

    res = ioctl (event_fds[0], FIONBIO, (char *)&nonblock);
    if (res < 0) {
        LOG_E ("socks5 tunnel event nonblock");
        return -1;
    }

    task_event = hev_task_new (-1);
    if (!task_event) {
        LOG_E ("socks5 tunnel task event");
        return -1;
    }

    return 0;
}

static void
event_task_fini (void)
{
    if (task_event) {
        hev_task_unref (task_event);
        task_event = NULL;
    }

    if (event_fds[0] >= 0) {
        close (event_fds[0]);
        event_fds[0] = -1;
    }
    if (event_fds[1] >= 0) {
        close (event_fds[1]);
        event_fds[1] = -1;
    }
}

static int
lwip_io_task_init (void)
{
    task_lwip_io = hev_task_new (-1);
    if (!task_lwip_io) {
        LOG_E ("socks5 tunnel task lwip");
        return -1;
    }
    hev_task_set_priority (task_lwip_io, 1);

    return 0;
}

static void
lwip_io_task_fini (void)
{
    if (task_lwip_io) {
        hev_task_unref (task_lwip_io);
        task_lwip_io = NULL;
    }
}

static int
lwip_timer_task_init (void)
{
    task_lwip_timer = hev_task_new (-1);
    if (!task_lwip_timer) {
        LOG_E ("socks5 tunnel task timer");
        return -1;
    }
    hev_task_set_priority (task_lwip_timer, 1);

    return 0;
}

static void
lwip_timer_task_fini (void)
{
    if (task_lwip_timer) {
        hev_task_unref (task_lwip_timer);
        task_lwip_timer = NULL;
    }
}

static int
mapped_dns_init (void)
{
    HevMappedDNS *dns;
    int cache_size;
    int network;
    int netmask;

    network = hev_config_get_mapdns_network ();
    netmask = hev_config_get_mapdns_netmask ();
    cache_size = hev_config_get_mapdns_cache_size ();

    if (!cache_size)
        return 0;

    dns = hev_mapped_dns_new (network, netmask, cache_size);
    if (!dns)
        return -1;

    hev_mapped_dns_put (dns);

    return 0;
}

static void
mapped_dns_fini (void)
{
    HevMappedDNS *dns;

    dns = hev_mapped_dns_get ();
    if (dns) {
        hev_object_unref (HEV_OBJECT (dns));
        hev_mapped_dns_put (NULL);
    }
}

int
hev_socks5_tunnel_init (int tun_fd)
{
    int res;

    LOG_D ("socks5 tunnel init");

    res = tunnel_init (tun_fd);
    if (res < 0)
        goto exit;

    res = gateway_init ();
    if (res < 0)
        goto exit;

    res = event_task_init ();
    if (res < 0)
        goto exit;

    res = lwip_io_task_init ();
    if (res < 0)
        goto exit;

    res = lwip_timer_task_init ();
    if (res < 0)
        goto exit;

    res = mapped_dns_init ();
    if (res < 0)
        goto exit;

    signal (SIGPIPE, SIG_IGN);

    hev_task_mutex_init (&mutex);

    return 0;

exit:
    hev_socks5_tunnel_fini ();
    return -1;
}

void
hev_socks5_tunnel_fini (void)
{
    LOG_D ("socks5 tunnel fini");

    mapped_dns_fini ();
    lwip_timer_task_fini ();
    lwip_io_task_fini ();
    event_task_fini ();
    gateway_fini ();
    tunnel_fini ();

    stat_tx_packets = 0;
    stat_rx_packets = 0;
    stat_tx_bytes = 0;
    stat_rx_bytes = 0;
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
    int res;
    int fd;

    LOG_D ("socks5 tunnel stop");

    for (;;) {
        fd = READ_ONCE (event_fds[1]);
        if (fd >= 0)
            break;
        /* Wait for async initialization */
        usleep (100 * 1000);
    }

    res = write (fd, &res, 1);
    assert (res > 0 && "socks5 tunnel write event");
}

void
hev_socks5_tunnel_stats (size_t *tx_packets, size_t *tx_bytes,
                         size_t *rx_packets, size_t *rx_bytes)
{
    LOG_D ("socks5 tunnel stats");

    if (tx_packets)
        *tx_packets = stat_tx_packets;

    if (tx_bytes)
        *tx_bytes = stat_tx_bytes;

    if (rx_packets)
        *rx_packets = stat_rx_packets;

    if (rx_bytes)
        *rx_bytes = stat_rx_bytes;
}
