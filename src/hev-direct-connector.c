#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>

#include <hev-task-io-poll.h>
#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-logger.h>

#include <hev-config.h>
#include <hev-direct-connector.h>
#include <hev-fallback-manager.h> // For HevFallbackContext
#include <hev-socks5-tunnel.h>

#define UDP_BUF_SIZE 2048

typedef struct _HevDirectConnectorTCPData HevDirectConnectorTCPData;
typedef struct _HevDirectConnectorUDPData HevDirectConnectorUDPData;

struct _HevDirectConnectorTCPData {
    struct tcp_pcb *pcb; // The lwip TCP PCB
    HevTaskMutex *mutex;
    HevFallbackContext *fallback_ctx;
    int native_fd; // The native socket file descriptor
    ip_addr_t target_ip;
    u16_t target_port;
};

struct _HevDirectConnectorUDPData {
    struct udp_pcb *pcb; // The lwip UDP PCB
    HevTaskMutex *mutex;
    struct pbuf *initial_pbuf; // Initial UDP packet (ref'd)
    ip_addr_t dest_addr;
    u16_t dest_port;
    ip_addr_t client_addr;
    u16_t client_port;
    int native_fd; // The native socket file descriptor
};

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    hev_task_yield (type);

    return hev_socks5_tunnel_is_running () ? 0 : -1;
}

static int
prepare_sockaddr (const ip_addr_t *addr, u16_t port,
                  struct sockaddr_storage *ss, socklen_t *sl)
{
    if (IP_IS_V4_VAL (*addr)) {
        struct sockaddr_in *s4 = (struct sockaddr_in *)ss;
        s4->sin_family = AF_INET;
        s4->sin_port = htons (port);
        s4->sin_addr.s_addr = addr->u_addr.ip4.addr;
        *sl = sizeof (struct sockaddr_in);
        return AF_INET;
    } else if (IP_IS_V6_VAL (*addr)) {
        struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)ss;
        s6->sin6_family = AF_INET6;
        s6->sin6_port = htons (port);
        s6->sin6_flowinfo = 0;
        s6->sin6_scope_id = 0;
        memcpy (&s6->sin6_addr, &addr->u_addr.ip6.addr,
                sizeof (s6->sin6_addr));
        *sl = sizeof (struct sockaddr_in6);
        return AF_INET6;
    }

    return AF_UNSPEC;
}

// --- TCP Direct Connector Implementation ---

static err_t
tcp_recv_cb (void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    HevDirectConnectorTCPData *task_data = (HevDirectConnectorTCPData *)arg;
    ssize_t s;

    LOG_D("Direct TCP: tcp_recv_cb called! p=%p, err=%d", p, err);

    if (err != ERR_OK) {
        LOG_E ("Direct TCP: lwip recv error: %d", err);
        tcp_abort (tpcb);
        if (task_data->fallback_ctx) {
            hev_fallback_context_signal_result (task_data->fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
        }
        return ERR_ABRT;
    }

    if (p == NULL) { // Connection closed by remote
        LOG_D ("Direct TCP: lwip connection closed.");
        close (task_data->native_fd);
        if (task_data->fallback_ctx) {
            hev_fallback_context_signal_result (task_data->fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
        }
        return ERR_OK;
    }

    struct iovec iov[16];
    int iovcnt = 0;
    size_t total_len = 0;
    for (struct pbuf *q = p; q != NULL && iovcnt < 16; q = q->next) {
        iov[iovcnt].iov_base = q->payload;
        iov[iovcnt].iov_len = q->len;
        total_len += q->len;
        iovcnt++;
    }

    s = hev_task_io_writev (task_data->native_fd, iov, iovcnt, task_io_yielder,
                            NULL);
    if (s <= 0) {
        LOG_E ("Direct TCP: writev to native_fd failed: %d", errno);
        tcp_abort (tpcb);
        close (task_data->native_fd);
        if (task_data->fallback_ctx) {
            hev_fallback_context_signal_result (
                task_data->fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
        }
        pbuf_free (p);
        return ERR_ABRT;
    }

    tcp_recved (tpcb, total_len);
    pbuf_free (p);
    return ERR_OK;
}

static void
tcp_err_cb (void *arg, err_t err)
{
    HevDirectConnectorTCPData *task_data = (HevDirectConnectorTCPData *)arg;
    LOG_E ("Direct TCP: lwip error: %d", err);
    close (task_data->native_fd);
    if (task_data->fallback_ctx) {
        hev_fallback_context_signal_result (task_data->fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
    }
}

static void
tcp_direct_proxy_task_entry (void *data)
{
    HevDirectConnectorTCPData *task_data = (HevDirectConnectorTCPData *)data;
    HevTask *self_task = hev_task_self();
    size_t buf_size;
    char *buffer;
    ssize_t s;

    buf_size = hev_config_get_misc_tcp_buffer_size ();
    buffer = hev_malloc (buf_size);
    if (!buffer) {
        LOG_E ("%p Direct TCP: proxy task failed to alloc buffer.", task_data);
        goto cleanup;
    }

    LOG_D ("%p Direct TCP: proxy task started for %s:%u, native_fd: %d", task_data,
           ipaddr_ntoa(&task_data->target_ip), task_data->target_port, task_data->native_fd);

    // Set lwip callbacks to this proxy
    tcp_arg (task_data->pcb, task_data);
    tcp_recv (task_data->pcb, tcp_recv_cb);
    tcp_err (task_data->pcb, tcp_err_cb);

    // Trigger lwip to send any pending data
    tcp_output (task_data->pcb);

    // Add native_fd to task system for reading
    hev_task_add_fd (self_task, task_data->native_fd, POLLIN);

    for (;;) {
        LOG_D ("%p Direct TCP: proxy task waiting for data on native_fd %d", task_data, task_data->native_fd);
        s = hev_task_io_read (task_data->native_fd, buffer, buf_size,
                              task_io_yielder, NULL);
        if (s <= 0) {
            if (s == 0) { // EOF, remote closed
                LOG_D ("%p Direct TCP: native_fd %d closed by remote.", task_data, task_data->native_fd);
            } else {
                LOG_E ("%p Direct TCP: read from native_fd %d failed: %d", task_data, task_data->native_fd, errno);
            }
            break;
        }

        LOG_D ("%p Direct TCP: proxy task read %zd bytes from native_fd %d", task_data, s, task_data->native_fd);

        // Write to lwip pcb
        hev_task_mutex_lock (task_data->mutex);
        err_t err = tcp_write (task_data->pcb, buffer, s, TCP_WRITE_FLAG_COPY);
        if (err == ERR_OK) {
            LOG_D ("%p Direct TCP: proxy task wrote %zd bytes to lwip pcb, calling tcp_output.", task_data, s);
            tcp_output (task_data->pcb);
        } else {
            LOG_E ("%p Direct TCP: lwip tcp_write failed: %d for %zd bytes", task_data, err, s);
        }
        hev_task_mutex_unlock (task_data->mutex);
    }

    hev_free (buffer);

cleanup:
    // Clean up
    hev_task_mutex_lock (task_data->mutex);
    tcp_abort (task_data->pcb);
    hev_task_mutex_unlock (task_data->mutex);
    close (task_data->native_fd);
    LOG_D ("%p Direct TCP: proxy task native_fd %d closed, pcb aborted.", task_data, task_data->native_fd);

    if (task_data->fallback_ctx) {
        hev_fallback_context_signal_result (task_data->fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
    }
    hev_free (task_data);
    LOG_D ("%p Direct TCP: proxy task finished.", task_data);
}

void
hev_direct_connector_tcp_try_connect (struct tcp_pcb *pcb, HevTaskMutex *mutex, HevFallbackContext *fallback_ctx)
{
    HevDirectConnectorTCPData *task_data;
    HevTask *task;
    int stack_size;
    int native_fd = -1;
    struct sockaddr_storage addr_storage;
    socklen_t addr_len;
    int family;
    int res;
    ip_addr_t target_ip;
    u16_t target_port;

    task_data = hev_malloc0 (sizeof (HevDirectConnectorTCPData));
    if (!task_data) {
        LOG_E ("Direct TCP: Failed to allocate task data.");
        goto fail_alloc_data;
    }

    task_data->pcb = pcb;
    task_data->mutex = mutex;
    task_data->fallback_ctx = fallback_ctx;

    target_ip = pcb->local_ip;
    target_port = pcb->local_port;

    task_data->target_ip = target_ip;
    task_data->target_port = target_port;

    family = prepare_sockaddr(&target_ip, target_port, &addr_storage, &addr_len);
    if (family == AF_UNSPEC) {
        LOG_E ("Direct TCP: Unknown IP address type.");
        goto fail_unknown_ip_type;
    }

    native_fd = socket (family, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (native_fd < 0) {
        LOG_E ("Direct TCP: Failed to create socket: %d", errno);
        goto fail_create_socket;
    }
    task_data->native_fd = native_fd;

    LOG_D ("%p Direct TCP: connecting to %s:%u", task_data,
           ipaddr_ntoa(&target_ip), target_port);

    LOG_D ("%p Direct TCP: attempting hev_task_io_connect to %s:%u on native_fd %d", task_data, ipaddr_ntoa(&target_ip), target_port, native_fd);

    int timeout;

    if (fallback_ctx)
        timeout = hev_config_get_smart_proxy_timeout_ms();
    else
        timeout = -1;

    res = hev_task_io_connect (native_fd, (struct sockaddr *)&addr_storage, addr_len, timeout, task_io_yielder, NULL);

    if (res < 0) {
        LOG_E ("%p Direct TCP: hev_task_io_connect failed with res %d, errno %d", task_data, res, errno);
        if (fallback_ctx) {
            if (errno == ETIMEDOUT) {
                hev_fallback_context_signal_result (fallback_ctx, HEV_FALLBACK_STATUS_TIMEOUT);
            } else {
                hev_fallback_context_signal_result (fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
            }
        }
        goto fail_connect;
    }

    LOG_D ("%p Direct TCP: native_fd %d connected. lwip pcb state: %d", task_data, native_fd, task_data->pcb->state);

    // Connection successful, create proxy task
    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        LOG_E ("Direct TCP: Failed to create proxy task.");
        goto fail_create_task;
    }

    hev_task_run (task, tcp_direct_proxy_task_entry, task_data);

    if (fallback_ctx) {
        hev_fallback_context_signal_result (fallback_ctx, HEV_FALLBACK_STATUS_SUCCESS);
    }
    return;

fail_create_task:
    close (native_fd);
fail_connect:
    close (native_fd);
fail_create_socket:
fail_unknown_ip_type:
    hev_free (task_data);
fail_alloc_data:
    // If we reach here, the pcb is still owned by lwip, and will be handled by tcp_accept_handler's return ERR_MEM
    // or by the fallback manager if it's managing the pcb.
    if (fallback_ctx) {
        // If we failed before signaling, ensure fallback_ctx is signaled
        hev_fallback_context_signal_result (fallback_ctx, HEV_FALLBACK_STATUS_FAILURE);
    }
    return;
}

// --- UDP Direct Connector Implementation ---

static err_t
udp_sendto_from (struct udp_pcb *pcb, struct pbuf *p,
                 const ip_addr_t *dst_ip, u16_t dst_port,
                 const ip_addr_t *src_ip, u16_t src_port)
{
    err_t err;
    ip_addr_t prev_local_ip;
    u16_t prev_local_port;

    ip_addr_copy (prev_local_ip, pcb->local_ip);
    prev_local_port = pcb->local_port;

    ip_addr_copy (pcb->local_ip, *src_ip);
    pcb->local_port = src_port;

    err = udp_sendto (pcb, p, dst_ip, dst_port);

    ip_addr_copy (pcb->local_ip, prev_local_ip);
    pcb->local_port = prev_local_port;

    return err;
}

static void
udp_direct_proxy_task_entry (void *data)
{
    HevDirectConnectorUDPData *task_data = (HevDirectConnectorUDPData *)data;
    HevTask *self_task = hev_task_self ();
    ssize_t s;
    err_t err;
    struct sockaddr_storage dest_sockaddr_storage;
    socklen_t dest_sockaddr_len;

    LOG_D ("%p Direct UDP: proxy task started for %s:%u", task_data,
           ipaddr_ntoa (&task_data->dest_addr), task_data->dest_port);

    // Prepare destination sockaddr for sendto
    if (prepare_sockaddr(&task_data->dest_addr, task_data->dest_port, &dest_sockaddr_storage, &dest_sockaddr_len) == AF_UNSPEC) {
        LOG_E ("%p Direct UDP: Unknown IP address type in proxy task.", task_data);
        if (task_data->initial_pbuf)
            pbuf_free (task_data->initial_pbuf);
        close (task_data->native_fd);
        hev_free (task_data);
        return;
    }

    // Send initial packet
    if (task_data->initial_pbuf) {
        s = hev_task_io_sendto (task_data->native_fd,
                               task_data->initial_pbuf->payload,
                               task_data->initial_pbuf->len, 0,
                               (struct sockaddr *)&dest_sockaddr_storage,
                               dest_sockaddr_len, task_io_yielder, NULL);
        if (s <= 0) {
            LOG_E ("%p Direct UDP: initial sendto failed: %d", task_data, errno);
        } else {
            LOG_D ("%p Direct UDP: initial sendto successful, sent %zd bytes.", task_data, s);
        }
        pbuf_free (task_data->initial_pbuf);
        task_data->initial_pbuf = NULL;
    }

    // Add native_fd to task system for reading
    hev_task_add_fd (self_task, task_data->native_fd, POLLIN);

    for (;;) {
        struct pbuf *pbuf_response = pbuf_alloc (PBUF_TRANSPORT, UDP_BUF_SIZE, PBUF_RAM);
        if (!pbuf_response) {
            LOG_E ("%p Direct UDP: pbuf_alloc failed", task_data);
            hev_task_sleep (10); // Avoid busy-loop
            continue;
        }

        // Per user request, receive without caring about the source
        s = hev_task_io_recvfrom (task_data->native_fd, pbuf_response->payload, pbuf_response->len, 0,
                                  NULL, NULL, task_io_yielder, NULL);
        if (s <= 0) {
            pbuf_free (pbuf_response);
            if (s < 0)
                LOG_E ("%p Direct UDP: recvfrom failed: %d", task_data, errno);
            else
                LOG_D ("%p Direct UDP: remote closed.", task_data);
            break;
        }

        pbuf_realloc (pbuf_response, s);

        LOG_D ("%p Direct UDP: received %zd bytes from remote.", task_data, s);

        hev_task_mutex_lock (task_data->mutex);
        err = udp_sendto_from (task_data->pcb, pbuf_response,
                               &task_data->client_addr, task_data->client_port,
                               &task_data->dest_addr, task_data->dest_port);
        hev_task_mutex_unlock (task_data->mutex);
        pbuf_free (pbuf_response);

        if (err != ERR_OK) {
            LOG_E ("%p Direct UDP: sendto client failed: %d", task_data, err);
        } else {
            char client_ip_str[IPADDR_STRLEN_MAX];
            char dest_ip_str[IPADDR_STRLEN_MAX];
            ipaddr_ntoa_r (&task_data->client_addr, client_ip_str,
                           sizeof (client_ip_str));
            ipaddr_ntoa_r (&task_data->dest_addr, dest_ip_str, sizeof (dest_ip_str));
            LOG_D ("%p Direct UDP: sent %zd bytes. dest:[%s:%u], client:[%s:%u]", task_data, s,
                   dest_ip_str, task_data->dest_port, client_ip_str,
                   task_data->client_port);
        }
    }

    // Clean up
    close (task_data->native_fd);
    hev_free (task_data);
}

void
hev_direct_connector_udp_run (struct udp_pcb *pcb, HevTaskMutex *mutex,
                                  struct pbuf *p, const ip_addr_t *addr,
                                  u16_t port, const ip_addr_t *client_addr,
                                  u16_t client_port)
{
    HevDirectConnectorUDPData *task_data;
    HevTask *task;
    int stack_size;
    int native_fd = -1;
    int family;

    task_data = hev_malloc0 (sizeof (HevDirectConnectorUDPData));
    if (!task_data) {
        LOG_E ("Direct UDP: Failed to allocate task data.");
        goto fail_alloc_data;
    }

    task_data->pcb = pcb;
    task_data->mutex = mutex;
    task_data->initial_pbuf = p;
    task_data->dest_addr = *addr;
    task_data->dest_port = port;
    task_data->client_addr = *client_addr;
    task_data->client_port = client_port;

    // Determine address family
    if (IP_IS_V4_VAL (*addr)) {
        family = AF_INET;
    } else if (IP_IS_V6_VAL (*addr)) {
        family = AF_INET6;
    } else {
        LOG_E ("Direct UDP: Unknown IP address type.");
        goto fail_unknown_ip_type;
    }

    native_fd = socket (family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (native_fd < 0) {
        LOG_E ("Direct UDP: Failed to create socket: %d", errno);
        goto fail_create_socket;
    }
    task_data->native_fd = native_fd;

    // NOTE: Per user request, the socket is not connected.

    LOG_D ("%p Direct UDP: task created for %s:%u.", task_data, ipaddr_ntoa (addr), port);

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        LOG_E ("Direct UDP: Failed to create proxy task.");
        goto fail_create_task;
    }

    pbuf_ref (p); // Ref the pbuf as it's passed to another task
    hev_task_run (task, udp_direct_proxy_task_entry, task_data);
    return;

fail_create_task:
    close (native_fd);
fail_create_socket:
fail_unknown_ip_type:
    hev_free (task_data);
fail_alloc_data:
    pbuf_free (p); // Free initial pbuf if we fail here
    return;
}
