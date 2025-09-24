#ifndef __HEV_FALLBACK_MANAGER_H__
#define __HEV_FALLBACK_MANAGER_H__

#include <lwip/ip_addr.h>
#include <lwip/tcp.h>
#include <hev-task-mutex.h>
#include <hev-list.h>
#include <time.h> // For time_t
#include <hev-task.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HEV_FALLBACK_TYPE_NONE,
    HEV_FALLBACK_TYPE_DIRECT,
    HEV_FALLBACK_TYPE_SOCKS5
} HevFallbackType;

typedef enum {
    HEV_FALLBACK_STATUS_SUCCESS,
    HEV_FALLBACK_STATUS_FAILURE,
    HEV_FALLBACK_STATUS_TIMEOUT // Specific failure for non-domestic direct
} HevFallbackStatus;

// Context for a single TCP connection's fallback attempt
typedef struct _HevFallbackContext {
    struct tcp_pcb *tcp_pcb; // The lwip TCP PCB
    ip_addr_t dest_addr;
    u16_t dest_port;
    HevTaskMutex *mutex;
    HevFallbackType current_attempt_type; // What we are currently trying
    HevTask *owner_task; // The _fallback_task_entry task
    int signal_fd[2]; // Pipe for signaling between _try_connect and owner_task
} HevFallbackContext;

// Structure for in-memory blocked IP list
typedef struct _HevBlockedIP {
    HevListNode node;
    ip_addr_t ip;
    time_t timestamp; // Time when this IP was added/last marked as blocked
} HevBlockedIP;

/**
 * hev_fallback_manager_init:
 *
 * Initializes the fallback manager and the blocked IP list.
 * Returns: 0 on success, -1 on failure.
 */
int hev_fallback_manager_init(void);

/**
 * hev_fallback_manager_fini:
 *
 * Finalizes the fallback manager and frees the blocked IP list.
 */
void hev_fallback_manager_fini(void);

/**
 * hev_fallback_manager_start_tcp:
 * @pcb: The lwip TCP PCB.
 * @mutex: The task system mutex.
 *
 * Starts the fallback process for a TCP connection.
 * This function creates and manages its own task (_fallback_task_entry).
 */
void hev_fallback_manager_start_tcp(struct tcp_pcb *pcb, HevTaskMutex *mutex);

/**
 * hev_fallback_context_signal_result:
 * @ctx: The fallback context.
 * @status: The result of the connection attempt (SUCCESS, FAILURE, TIMEOUT).
 *
 * Signals to the fallback manager that a connection attempt completed with a result.
 */
void hev_fallback_context_signal_result(HevFallbackContext *ctx, HevFallbackStatus status);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_FALLBACK_MANAGER_H__ */
