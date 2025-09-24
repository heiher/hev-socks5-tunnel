#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <hev-task-dns.h>
#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-memory-allocator.h>

#include <hev-config.h>
#include <hev-logger.h>
#include <hev-config-const.h>

#include <hev-dns-forwarder.h>


typedef struct _HevDNSForwarderTaskData HevDNSForwarderTaskData;

struct _HevDNSForwarderTaskData {
    struct udp_pcb *pcb;      // The listening pcb
    HevTaskMutex *mutex;
    struct pbuf *query_pbuf;  // The query packet
    ip_addr_t client_addr;    // The client's address
    u16_t client_port;        // The client's port
    int family;               // The client's address family
};

static int
parse_address (const char *address, char *host, size_t host_len, char *port,
               size_t port_len)
{
    const char *p;
    const char *h = address;
    size_t n;

    strncpy (port, "53", port_len - 1);
    port[port_len - 1] = '\0';

    if (*address == '[') {
        p = strchr (address, ']');
        if (!p)
            return -1;
        h = address + 1;
        n = p - h;
        if (n >= host_len)
            return -1;
        memcpy (host, h, n);
        host[n] = '\0';
        if (p[1] == ':') {
            strncpy (port, p + 2, port_len - 1);
            port[port_len - 1] = '\0';
        }
    } else {
        p = strrchr (address, ':');
        if (p && strchr (address, ':') == p) {
            n = p - address;
            if (n >= host_len)
                return -1;
            memcpy (host, address, n);
            host[n] = '\0';
            strncpy (port, p + 1, port_len - 1);
            port[port_len - 1] = '\0';
        } else {
            strncpy (host, address, host_len - 1);
            host[host_len - 1] = '\0';
        }
    }

    return 0;
}

static void
dns_forwarder_task_entry (void *data)
{
    HevDNSForwarderTaskData *task_data = data;
    HevTask *task = hev_task_self ();
    const char *target_str;
    struct addrinfo hints = { 0 };
    struct addrinfo *res = NULL, *rp;
    unsigned char response_buf[UDP_BUF_SIZE];
    char target_host[128];
    char target_port[16];
    int fd = -1;
    int s;
    err_t err;

    if (task_data->family == AF_INET)
        target_str = hev_config_get_dns_forwarder_target4 ();
    else
        target_str = hev_config_get_dns_forwarder_target6 ();

    if (!target_str) {
        LOG_E ("%p dns fwd: no target configured for family %d", task_data, task_data->family);
        goto exit;
    }

    if (parse_address (target_str, target_host, sizeof (target_host),
                       target_port, sizeof (target_port)) < 0) {
        LOG_E ("%p dns fwd: invalid target address", task_data);
        goto exit;
    }

    char client_ip_str[IPADDR_STRLEN_MAX];
    ipaddr_ntoa_r(&task_data->client_addr, client_ip_str, sizeof(client_ip_str));
    LOG_D("%p dns fwd: query from %s:%u => %s:[%s]", task_data, client_ip_str, task_data->client_port, target_host, target_port);

    hints.ai_family = task_data->family;
    hints.ai_socktype = SOCK_DGRAM;

    s = hev_task_dns_getaddrinfo (target_host, target_port, &hints, &res);
    if (s != 0) {
        LOG_E ("%p dns fwd: getaddrinfo: %s", task_data, gai_strerror(s));
        res = NULL; // Ensure res is null on failure
        goto exit;
    }

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        fd = socket (rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK,
                     rp->ai_protocol);
        if (fd >= 0)
            break;
    }

    if (rp == NULL) {
        LOG_E ("%p dns fwd: socket", task_data);
        goto exit;
    }

    s = hev_task_add_fd (task, fd, POLLIN);
    if (s < 0) {
        LOG_E ("%p dns fwd: add fd", task_data);
        goto exit;
    }

    s = sendto (fd, task_data->query_pbuf->payload, task_data->query_pbuf->len, 0,
                rp->ai_addr, rp->ai_addrlen);
    if (s <= 0) {
        LOG_E ("%p dns fwd: sendto", task_data);
        goto exit;
    }

    pbuf_free (task_data->query_pbuf);
    task_data->query_pbuf = NULL;

    if (hev_task_sleep (hev_config_get_misc_connect_timeout ()) <= 0) {
        LOG_W ("%p dns fwd: timeout", task_data);
        goto exit;
    }

    ssize_t len = recvfrom (fd, response_buf, sizeof (response_buf), 0, NULL, NULL);
    if (len <= 0) {
        LOG_E ("%p dns fwd: recvfrom", task_data);
        goto exit;
    }

    LOG_D ("%p dns fwd: received %zd bytes from %s:[%s]", task_data, len, target_host,
           target_port);

    struct pbuf *pbuf_response = pbuf_alloc (PBUF_TRANSPORT, len, PBUF_RAM);
    if (!pbuf_response) {
        LOG_E ("%p dns fwd: pbuf_alloc", task_data);
        goto exit;
    }

    memcpy (pbuf_response->payload, response_buf, len);
    hev_task_mutex_lock (task_data->mutex);
    err = udp_sendto (task_data->pcb, pbuf_response, &task_data->client_addr,
                      task_data->client_port);
    hev_task_mutex_unlock (task_data->mutex);
    pbuf_free (pbuf_response);

    if (err != ERR_OK) {
        LOG_E ("%p dns fwd: sendto client: %d", task_data, err);
    } else {
        LOG_D ("%p dns fwd: sent %zd bytes to %s:%u", task_data, len, client_ip_str,
               task_data->client_port);
    }

exit:
    if (res)
        freeaddrinfo (res);
    if (fd >= 0)
        close (fd);
    if (task_data->query_pbuf)
        pbuf_free (task_data->query_pbuf);
    hev_free (task_data);
}

void
hev_dns_forwarder_run (struct udp_pcb *pcb, HevTaskMutex *mutex, struct pbuf *p,
                       const ip_addr_t *addr, u16_t port, int family)
{
    HevDNSForwarderTaskData *task_data;
    HevTask *task;
    int stack_size;

    task_data = hev_malloc (sizeof (HevDNSForwarderTaskData));
    if (!task_data) {
        LOG_E ("dns fwd: alloc data");
        return;
    }

    task_data->pcb = pcb;
    task_data->mutex = mutex;
    task_data->query_pbuf = p;
    task_data->client_addr = *addr;
    task_data->client_port = port;
    task_data->family = family;

    pbuf_ref (p); // The caller will free the original pbuf, so we ref it.

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        pbuf_free (p);
        LOG_E ("%p dns fwd: new task", task_data);
        hev_free (task_data);
        return;
    }

    hev_task_run (task, dns_forwarder_task_entry, task_data);
}
