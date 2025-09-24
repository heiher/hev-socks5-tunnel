#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <lwip/tcp.h>
#include <lwip/ip_addr.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-mutex.h>
#include <hev-memory-allocator.h>
#include <hev-logger.h>
#include <hev-list.h> // Corrected include path
#include <hev-utils.h> // Corrected include path
#include <hev-compiler.h> // Corrected include path

#include <hev-config.h>
#include <hev-chnroutes-manager.h>
#include <hev-direct-connector.h>
#include <hev-socks5-session-tcp.h>
#include <hev-socks5-tunnel.h>
#include <hev-fallback-manager.h>

static HevList blocked_ips;
static int is_initialized = 0;

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    hev_task_yield (type);
    return hev_socks5_tunnel_is_running () ? 0 : -1;
}

static void
hev_fallback_manager_add_blocked_ip (const ip_addr_t *ip)
{
    HevBlockedIP *blocked_ip = NULL;
    HevListNode *node;

    // Check if already in list and update timestamp
    for (node = hev_list_first (&blocked_ips); node; node = hev_list_node_next (node)) {
        blocked_ip = container_of (node, HevBlockedIP, node);
        if (ip_addr_cmp (&blocked_ip->ip, ip)) {
            blocked_ip->timestamp = time (NULL);
            LOG_D ("Fallback: Updated timestamp for blocked IP %s", ipaddr_ntoa (ip));
            return;
        }
    }

    // Add new entry
    blocked_ip = hev_malloc0 (sizeof (HevBlockedIP));
    if (!blocked_ip) {
        LOG_E ("Fallback: Failed to allocate blocked IP entry.");
        return;
    }

    blocked_ip->ip = *ip;
    blocked_ip->timestamp = time (NULL);
    hev_list_add_tail (&blocked_ips, &blocked_ip->node);
    LOG_D ("Fallback: Added %s to blocked IP list.", ipaddr_ntoa (ip));
}

static int
hev_fallback_manager_is_blocked_ip (const ip_addr_t *ip)
{
    HevBlockedIP *blocked_ip = NULL;
    HevListNode *node;
    time_t current_time = time (NULL);
    unsigned int expiry_minutes = hev_config_get_smart_proxy_blocked_ip_expiry_minutes ();

    node = hev_list_first (&blocked_ips);
    while (node) {
        blocked_ip = container_of (node, HevBlockedIP, node);
        node = hev_list_node_next (node); // Get next before potential removal

        if (expiry_minutes > 0 && (current_time - blocked_ip->timestamp) > (expiry_minutes * 60)) {
            LOG_D ("Fallback: Removing expired blocked IP %s", ipaddr_ntoa (&blocked_ip->ip));
            hev_list_del (&blocked_ips, &blocked_ip->node);
            hev_free (blocked_ip);
        } else if (ip_addr_cmp (&blocked_ip->ip, ip)) {
            return 1; // Found and not expired
        }
    }

    return 0; // Not found or all expired
}

static HevFallbackStatus
wait_for_signal (HevFallbackContext *ctx, const char *from)
{
    char signal_byte;
    ssize_t res;

    res = hev_task_io_read (ctx->signal_fd[0], &signal_byte, 1, task_io_yielder, NULL);
    if (res <= 0) {
        LOG_E ("%p Fallback: Failed to read signal from %s: %d", ctx, from, errno);
        return HEV_FALLBACK_STATUS_FAILURE;
    }

    return (HevFallbackStatus)signal_byte;
}



static void
fallback_task_entry (void *data)
{
    HevFallbackContext *ctx = (HevFallbackContext *)data;
    HevFallbackStatus status;

    LOG_D ("%p Fallback: Task started for %s:%u", ctx, ipaddr_ntoa(&ctx->dest_addr), ctx->dest_port);

    // Determine initial attempt type
    int is_domestic = hev_chnroutes_manager_is_domestic(&ctx->dest_addr);
    if (is_domestic == -1) { // Chnroutes error/not initialized
        LOG_W("%p Fallback: Chnroutes manager error. Treating as non-domestic.", ctx);
        is_domestic = 0;
    }

    if (is_domestic == 1) {
        // Domestic IP: Always direct, no fallback
        LOG_D ("%p Fallback: Domestic IP, attempting direct connect.", ctx);
        ctx->current_attempt_type = HEV_FALLBACK_TYPE_DIRECT;
        hev_direct_connector_tcp_try_connect (ctx->tcp_pcb, ctx->mutex, ctx);
    } else {
        // Non-domestic IP: Smart proxy logic
        if (hev_fallback_manager_is_blocked_ip(&ctx->dest_addr)) {
            // IP is blocked, go straight to SOCKS5
            LOG_D ("%p Fallback: Non-domestic IP %s is blocked, attempting SOCKS5 connect.", ctx, ipaddr_ntoa(&ctx->dest_addr));
            ctx->current_attempt_type = HEV_FALLBACK_TYPE_SOCKS5;
            hev_socks5_session_tcp_try_connect (ctx->tcp_pcb, ctx->mutex, ctx);
        } else {
            // IP not blocked, first attempt direct
            LOG_D ("%p Fallback: Non-domestic IP %s not blocked, first attempting direct connect.", ctx, ipaddr_ntoa(&ctx->dest_addr));
            ctx->current_attempt_type = HEV_FALLBACK_TYPE_DIRECT;
            hev_direct_connector_tcp_try_connect (ctx->tcp_pcb, ctx->mutex, ctx);
        }
    }

    status = wait_for_signal (ctx, "initial connect");
    if (status == HEV_FALLBACK_STATUS_SUCCESS) {
        LOG_D ("%p Fallback: Connection successful via %s.", ctx,
               (ctx->current_attempt_type == HEV_FALLBACK_TYPE_DIRECT) ? "Direct" : "SOCKS5");
        ctx->tcp_pcb = NULL;
        goto exit;
    }

    // Handle failure/timeout
    if (is_domestic == 1) {
        // Domestic IP failed, no fallback
        LOG_E ("%p Fallback: Domestic direct connect failed for %s:%u. No fallback.",
               ctx, ipaddr_ntoa(&ctx->dest_addr), ctx->dest_port);
        goto exit;
    }

    // Non-domestic IP failure/timeout, try SOCKS5 as fallback
    if (ctx->current_attempt_type == HEV_FALLBACK_TYPE_DIRECT && (status == HEV_FALLBACK_STATUS_TIMEOUT || status == HEV_FALLBACK_STATUS_FAILURE)) {
        LOG_W ("%p Fallback: Direct connect to %s:%u timed out. Adding to blocked list and trying SOCKS5.",
               ctx, ipaddr_ntoa(&ctx->dest_addr), ctx->dest_port);
        hev_fallback_manager_add_blocked_ip(&ctx->dest_addr);

        ctx->current_attempt_type = HEV_FALLBACK_TYPE_SOCKS5;
        hev_socks5_session_tcp_try_connect (ctx->tcp_pcb, ctx->mutex, ctx);

        status = wait_for_signal (ctx, "SOCKS5 fallback");
        if (status == HEV_FALLBACK_STATUS_SUCCESS) {
            LOG_D ("%p Fallback: SOCKS5 fallback successful for %s:%u.",
                   ctx, ipaddr_ntoa(&ctx->dest_addr), ctx->dest_port);
            ctx->tcp_pcb = NULL;
            goto exit;
        }
    }

    LOG_E ("%p Fallback: All connection attempts failed for %s:%u.",
           ctx, ipaddr_ntoa(&ctx->dest_addr), ctx->dest_port);

exit:
    // Clean up pcb if not taken over by a successful proxy task
    if (ctx->tcp_pcb) {
        hev_task_mutex_lock(ctx->mutex);
        tcp_abort(ctx->tcp_pcb);
        hev_task_mutex_unlock(ctx->mutex);
    }
    close (ctx->signal_fd[0]);
    close (ctx->signal_fd[1]);
    hev_free (ctx);
}

int
hev_fallback_manager_init (void)
{
    if (is_initialized) {
        LOG_W ("Fallback manager already initialized.");
        return 0;
    }

    hev_list_init (&blocked_ips);
    is_initialized = 1;
    LOG_D ("Fallback manager initialized.");
    return 0;
}

void
hev_fallback_manager_fini (void)
{
    if (!is_initialized) return;

    HevBlockedIP *blocked_ip;
    HevListNode *node;

    node = hev_list_first (&blocked_ips);
    while (node) {
        blocked_ip = container_of (node, HevBlockedIP, node);
        node = hev_list_node_next (node);
        hev_list_del (&blocked_ips, &blocked_ip->node);
        hev_free (blocked_ip);
    }
    is_initialized = 0;
    LOG_D ("Fallback manager finalized.");
}

void
hev_fallback_manager_start_tcp (struct tcp_pcb *pcb, HevTaskMutex *mutex)
{
    HevFallbackContext *ctx;
    HevTask *task;
    int stack_size;

    if (!is_initialized) {
        LOG_E ("Fallback manager not initialized.");
        tcp_abort(pcb);
        return;
    }

    ctx = hev_malloc0 (sizeof (HevFallbackContext));
    if (!ctx) {
        LOG_E ("%p Fallback: Failed to allocate context.", pcb);
        tcp_abort(pcb);
        return;
    }

    ctx->tcp_pcb = pcb;
    ctx->mutex = mutex;
    ctx->dest_addr = pcb->local_ip;
    ctx->dest_port = pcb->local_port;

    if (pipe (ctx->signal_fd) < 0) {
        LOG_E ("%p Fallback: Failed to create signal pipe: %d", ctx, errno);
        hev_free (ctx);
        tcp_abort(pcb);
        return;
    }

    // Set pipe read end to non-blocking for hev_task_io_read
    int flags = fcntl(ctx->signal_fd[0], F_GETFL, 0);
    fcntl(ctx->signal_fd[0], F_SETFL, flags | O_NONBLOCK);

    stack_size = hev_config_get_misc_task_stack_size ();
    task = hev_task_new (stack_size);
    if (!task) {
        LOG_E ("%p Fallback: Failed to create fallback task.", ctx);
        close (ctx->signal_fd[0]);
        close (ctx->signal_fd[1]);
        hev_free (ctx);
        tcp_abort(pcb);
        return;
    }

    hev_task_run (task, fallback_task_entry, ctx);
}

void
hev_fallback_context_signal_result (HevFallbackContext *ctx, HevFallbackStatus status)
{
    if (!ctx) return;

    char signal_byte = (char)status;
    ssize_t res = write (ctx->signal_fd[1], &signal_byte, 1);
    if (res <= 0) {
        LOG_E ("%p Fallback: Failed to write signal to pipe: %d", ctx, errno);
    }
}
