/*
 ============================================================================
 Name        : hev-tunnel-linux.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2019 - 2023 hev
 Description : Tunnel on Linux
 ============================================================================
 */

#if defined(__linux__)

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <linux/ipv6.h>
#include <linux/if_tun.h>
#include <linux/virtio_net.h>

#include <hev-task.h>
#include <hev-task-io.h>

#include "hev-logger.h"
#include "hev-tunnel.h"

#define VNET_BUF_SIZE (65535)
#define VNET_HDR_MAX (32)

#ifndef TUN_F_USO4
#define TUN_F_USO4 0x20
#define TUN_F_USO6 0x40
#endif

#ifndef VIRTIO_NET_HDR_GSO_UDP_L4
#define VIRTIO_NET_HDR_GSO_UDP_L4 5
#endif

static char tun_name[IFNAMSIZ];
static int tun_vnet_hdr_sz;
static int tun_offload;
static struct pbuf *tun_rbuf;

int
hev_tunnel_open (const char *name, int multi_queue, const char *guid)
{
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = hev_task_io_open ("/dev/net/tun", O_RDWR);
    if (fd < 0)
        goto exit;

    ifr.ifr_flags = IFF_TUN | IFF_NO_PI | IFF_VNET_HDR;
    if (multi_queue)
        ifr.ifr_flags |= IFF_MULTI_QUEUE;
    if (name)
        strncpy (ifr.ifr_name, name, IFNAMSIZ - 1);

    res = ioctl (fd, TUNSETIFF, (void *)&ifr);
    if (res < 0) {
        ifr.ifr_flags &= ~IFF_VNET_HDR;
        res = ioctl (fd, TUNSETIFF, (void *)&ifr);
        if (res < 0)
            goto exit_close;
    }

    memcpy (tun_name, ifr.ifr_name, IFNAMSIZ);
    return fd;

exit_close:
    close (fd);
exit:
    return res;
}

void
hev_tunnel_close (int fd)
{
    hev_tunnel_set_offload (-1, 0);
    close (fd);
}

void
hev_tunnel_set_offload (int fd, int offload)
{
    const unsigned int tso = TUN_F_CSUM | TUN_F_TSO4 | TUN_F_TSO6;
    const unsigned int uso = TUN_F_USO4 | TUN_F_USO6;
    struct ifreq ifr = { 0 };
    unsigned int rx = TUN_F_CSUM;
    int size;

    if (tun_rbuf) {
        pbuf_free (tun_rbuf);
        tun_rbuf = NULL;
    }
    tun_vnet_hdr_sz = 0;
    tun_offload = 0;

    /*
     * Everything is probed on the fd, which may also come from outside
     * (e.g. Android VpnService). Any failure (old or broken kernels, denied
     * ioctls) falls back to plain packets. The segmentation offloads can be
     * disabled by the configuration.
     */
    if ((fd < 0) || (ioctl (fd, TUNGETIFF, &ifr) < 0))
        return;
    if (!(ifr.ifr_flags & IFF_VNET_HDR)) {
        LOG_I ("socks5 tunnel offload: -tso -uso");
        return;
    }

    if (ioctl (fd, TUNGETVNETHDRSZ, &size) < 0)
        return;
    if (size != sizeof (struct virtio_net_hdr)) {
        int hsize = sizeof (struct virtio_net_hdr);
        if (ioctl (fd, TUNSETVNETHDRSZ, &hsize) == 0)
            size = hsize;
    }
    if ((size < sizeof (struct virtio_net_hdr)) || (size > VNET_HDR_MAX))
        return;

    tun_vnet_hdr_sz = size;

    tun_offload = HEV_TUNNEL_OFFLOAD_CSUM;
    tun_offload |= offload & HEV_TUNNEL_OFFLOAD_TCP;
    if (offload & HEV_TUNNEL_OFFLOAD_TCP)
        rx = tso;

    /* Kernels that know USO accept UDP GSO packets (Linux 6.2+). */
    if ((offload & HEV_TUNNEL_OFFLOAD_UDP) &&
        (ioctl (fd, TUNSETOFFLOAD, rx | uso) == 0))
        tun_offload |= HEV_TUNNEL_OFFLOAD_UDP;

    /* Receive TSO packets if enabled, USO packets are not handled. */
    if (ioctl (fd, TUNSETOFFLOAD, rx) < 0)
        if (ioctl (fd, TUNSETOFFLOAD, TUN_F_CSUM) < 0)
            ioctl (fd, TUNSETOFFLOAD, 0);

    LOG_I ("socks5 tunnel offload: %ctso %cuso",
           (tun_offload & HEV_TUNNEL_OFFLOAD_TCP) ? '+' : '-',
           (tun_offload & HEV_TUNNEL_OFFLOAD_UDP) ? '+' : '-');
}

int
hev_tunnel_get_offload (void)
{
    return tun_offload;
}

int
hev_tunnel_set_mtu (int mtu)
{
    int fd, res = -1;
    struct ifreq ifr = { .ifr_mtu = mtu };

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCSIFMTU, (void *)&ifr);

    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_state (int state)
{
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCGIFFLAGS, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    if (state)
        ifr.ifr_flags |= IFF_UP;
    else
        ifr.ifr_flags &= ~IFF_UP;
    res = ioctl (fd, SIOCSIFFLAGS, (void *)&ifr);

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_ipv4 (const char *addr, unsigned int prefix)
{
    struct ifreq ifr = { 0 };
    struct sockaddr_in *pa;
    int res = -1;
    int fd;

    fd = socket (AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);

    pa = (struct sockaddr_in *)&ifr.ifr_addr;
    pa->sin_family = AF_INET;
    res = inet_pton (AF_INET, addr, &pa->sin_addr);
    if (!res)
        goto exit_close;
    res = ioctl (fd, SIOCSIFADDR, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    pa = (struct sockaddr_in *)&ifr.ifr_netmask;
    pa->sin_family = AF_INET;
    pa->sin_addr.s_addr = htonl (((unsigned int)(-1)) << (32 - prefix));
    res = ioctl (fd, SIOCSIFNETMASK, (void *)&ifr);
    if ((res < 0) && (errno == EEXIST))
        res = 0;

exit_close:
    close (fd);
exit:
    return res;
}

int
hev_tunnel_set_ipv6 (const char *addr, unsigned int prefix)
{
    struct in6_ifreq ifr6 = { 0 };
    struct ifreq ifr = { 0 };
    int res = -1;
    int fd;

    fd = socket (AF_INET6, SOCK_STREAM, 0);
    if (fd < 0)
        goto exit;

    memcpy (ifr.ifr_name, tun_name, IFNAMSIZ);
    res = ioctl (fd, SIOCGIFINDEX, (void *)&ifr);
    if (res < 0)
        goto exit_close;

    ifr6.ifr6_prefixlen = prefix;
    ifr6.ifr6_ifindex = ifr.ifr_ifindex;
    res = inet_pton (AF_INET6, addr, &ifr6.ifr6_addr);
    if (!res)
        goto exit_close;
    res = ioctl (fd, SIOCSIFADDR, (void *)&ifr6);
    if ((res < 0) && (errno == EEXIST))
        res = 0;

exit_close:
    close (fd);
exit:
    return res;
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
    unsigned int index;

    index = if_nametoindex (tun_name);
    snprintf (tun_index, sizeof (tun_index), "%d", index);
    return tun_index;
}

int
hev_tunnel_add_task (int fd, HevTask *task)
{
    return hev_task_add_fd (task, fd, POLLIN);
}

void
hev_tunnel_del_task (int fd, HevTask *task)
{
    hev_task_del_fd (task, fd);
}

static struct pbuf *
hev_tunnel_read_vnet (int fd, int mtu, HevTaskIOYielder yielder,
                      void *yielder_data)
{
    uint8_t hdr[VNET_HDR_MAX];
    struct iovec iov[3];
    struct pbuf *buf;
    int iovc = 2;
    ssize_t s;

    if (!tun_rbuf && (mtu < VNET_BUF_SIZE)) {
        tun_rbuf = pbuf_alloc (PBUF_RAW, VNET_BUF_SIZE - mtu, PBUF_RAM);
        if (!tun_rbuf)
            return NULL;
    }

    buf = pbuf_alloc (PBUF_RAW, mtu, PBUF_RAM);
    if (!buf)
        return NULL;

    /*
     * Regular packets land in the MTU-sized pbuf, the remainder of offloaded
     * (TSO) packets spills over into the spare pbuf, which is then chained.
     */
    iov[0].iov_base = hdr;
    iov[0].iov_len = tun_vnet_hdr_sz;
    iov[1].iov_base = buf->payload;
    iov[1].iov_len = mtu;
    if (tun_rbuf) {
        iov[2].iov_base = tun_rbuf->payload;
        iov[2].iov_len = tun_rbuf->len;
        iovc++;
    }

    s = hev_task_io_readv (fd, iov, iovc, yielder, yielder_data);
    if (s <= tun_vnet_hdr_sz) {
        pbuf_free (buf);
        return NULL;
    }
    s -= tun_vnet_hdr_sz;

    /* USO packets can't be passed to lwIP, they only show up if the
     * offloads couldn't be configured by us. */
    if (((struct virtio_net_hdr *)hdr)->gso_type == VIRTIO_NET_HDR_GSO_UDP_L4) {
        pbuf_free (buf);
        return NULL;
    }

    if (s <= mtu) {
        buf->tot_len = s;
        buf->len = s;
        return buf;
    }

    tun_rbuf->tot_len = s - mtu;
    tun_rbuf->len = s - mtu;
    pbuf_cat (buf, tun_rbuf);
    tun_rbuf = NULL;

    return buf;
}

struct pbuf *
hev_tunnel_read (int fd, int mtu, HevTaskIOYielder yielder, void *yielder_data)
{
    struct pbuf *buf;
    ssize_t s;

    if (tun_vnet_hdr_sz)
        return hev_tunnel_read_vnet (fd, mtu, yielder, yielder_data);

    buf = pbuf_alloc (PBUF_RAW, mtu, PBUF_RAM);
    if (!buf)
        return NULL;

    s = hev_task_io_read (fd, buf->payload, buf->len, yielder, yielder_data);
    if (s <= 0) {
        pbuf_free (buf);
        return NULL;
    }

    buf->tot_len = s;
    buf->len = s;

    return buf;
}

static void
hev_tunnel_fill_vnet_hdr (struct virtio_net_hdr *hdr, struct pbuf *buf)
{
    unsigned int iphl, tcphl, len, i;
    uint8_t *data = buf->payload;
    const uint8_t *addr;
    uint32_t sum = 0;
    uint16_t *csum;
    uint8_t proto;
    int gso_type;
    int alen;

    if (buf->len < 20)
        return;

    switch (data[0] >> 4) {
    case 4:
        iphl = (data[0] & 0xf) << 2;
        len = ((data[2] << 8) | data[3]) - iphl;
        proto = data[9];
        addr = &data[12];
        alen = 8;
        gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
        break;
    case 6:
        iphl = 40;
        len = (data[4] << 8) | data[5];
        proto = data[6];
        addr = &data[8];
        alen = 32;
        gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
        break;
    default:
        return;
    }

    switch (proto) {
    case IPPROTO_TCP:
        if (buf->len < (iphl + 20))
            return;
        hdr->csum_offset = 16;
        break;
    case IPPROTO_UDP:
        if (buf->len < (iphl + 8))
            return;
        hdr->csum_offset = 6;
        break;
    default:
        return;
    }

    /*
     * The TCP/UDP checksum is offloaded: store the pseudo header checksum and
     * let the kernel (or the NIC) finish it.
     */
    for (i = 0; i < alen; i += 2)
        sum += *(uint16_t *)&addr[i];
    sum += htons (proto) + htons (len);
    sum = (sum >> 16) + (sum & 0xffff);
    sum += sum >> 16;
    csum = (uint16_t *)&data[iphl + hdr->csum_offset];
    *csum = sum;

    hdr->flags = VIRTIO_NET_HDR_F_NEEDS_CSUM;
    hdr->csum_start = iphl;

    if (proto == IPPROTO_UDP) {
        if (!(tun_offload & HEV_TUNNEL_OFFLOAD_UDP))
            return;
        if (buf->gso_size && ((len - 8) > buf->gso_size)) {
            hdr->gso_type = VIRTIO_NET_HDR_GSO_UDP_L4;
            hdr->hdr_len = iphl + 8;
            hdr->gso_size = buf->gso_size;
        }
        return;
    }

    /*
     * Copy the headers into the linear part of the skb, the payload goes to
     * the page frags. Otherwise the kernel copies only up to the checksum
     * field, so the TCP header has to be pulled later and a payload of 8K
     * plus a few bytes spills over into an extra page.
     */
    tcphl = (data[iphl + 12] >> 4) << 2;
    hdr->hdr_len = iphl + tcphl;

    if (!(tun_offload & HEV_TUNNEL_OFFLOAD_TCP))
        return;

    if (buf->gso_size && ((len - tcphl) > buf->gso_size)) {
        hdr->gso_type = gso_type;
        hdr->gso_size = buf->gso_size;
    }
}

ssize_t
hev_tunnel_write (int fd, struct pbuf *buf)
{
    union
    {
        struct virtio_net_hdr hdr;
        uint8_t data[VNET_HDR_MAX];
    } vnet = { 0 };
    struct iovec iov[512];
    struct pbuf *p = buf;
    ssize_t res;
    int i = 0;

    if (tun_vnet_hdr_sz) {
        hev_tunnel_fill_vnet_hdr (&vnet.hdr, buf);
        iov[0].iov_base = &vnet;
        iov[0].iov_len = tun_vnet_hdr_sz;
        i++;
    } else if (!p->next) {
        return write (fd, p->payload, p->len);
    }

    for (; p && (i < 512); p = p->next) {
        iov[i].iov_base = p->payload;
        iov[i].iov_len = p->len;
        i++;
    }

    res = writev (fd, iov, i);
    if (!tun_vnet_hdr_sz)
        return res;

    if ((res < 0) && (errno == EINVAL)) {
        /*
         * The kernel rejects GSO packets of this kind, stop sending them.
         * A TCP packet is resent as is, UDP datagrams are resent one by one
         * by the session.
         */
        switch (vnet.hdr.gso_type) {
        case VIRTIO_NET_HDR_GSO_TCPV4:
        case VIRTIO_NET_HDR_GSO_TCPV6:
            LOG_W ("socks5 tunnel tcp gso disabled");
            tun_offload &= ~HEV_TUNNEL_OFFLOAD_TCP;
            memset (&vnet, 0, sizeof (vnet));
            hev_tunnel_fill_vnet_hdr (&vnet.hdr, buf);
            res = writev (fd, iov, i);
            break;
        case VIRTIO_NET_HDR_GSO_UDP_L4:
            LOG_W ("socks5 tunnel udp gso disabled");
            tun_offload &= ~HEV_TUNNEL_OFFLOAD_UDP;
            break;
        }
    }

    if (res <= tun_vnet_hdr_sz)
        return -1;

    return res - tun_vnet_hdr_sz;
}

#endif /* __linux__ */
