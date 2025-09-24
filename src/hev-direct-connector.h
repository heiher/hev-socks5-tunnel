#ifndef __HEV_DIRECT_CONNECTOR_H__
#define __HEV_DIRECT_CONNECTOR_H__

#include <lwip/tcp.h>
#include <lwip/udp.h>
#include <hev-task-mutex.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations for fallback (only used by TCP path)
typedef struct _HevFallbackContext HevFallbackContext;

/**
 * hev_direct_connector_tcp_try_connect:
 * @pcb: The lwip TCP PCB representing the incoming connection from the app.
 * @mutex: The task system mutex.
 * @fallback_ctx: Context for fallback management. NULL if no fallback is active.
 *
 * Attempts a direct TCP connection. If successful, proxies data.
 * If fallback_ctx is provided and smart proxy is enabled, it will signal
 * the fallback context on failure/timeout.
 * This function creates and manages its own task.
 */
void hev_direct_connector_tcp_try_connect(struct tcp_pcb *pcb, HevTaskMutex *mutex, HevFallbackContext *fallback_ctx);

/**
 * hev_direct_connector_udp_run:
 * @pcb: The lwip UDP PCB to send replies back to.
 * @mutex: The task system mutex.
 * @p: The initial UDP packet received from the app (ref'd).
 * @addr: The destination IP address of the UDP packet.
 * @port: The destination port of the UDP packet.
 * @client_addr: The original source IP address of the client app.
 * @client_port: The original source port of the client app.
 *
 * Creates a task to proxy UDP packets between the client and the direct remote.
 * This function creates and manages its own task.
 */
void hev_direct_connector_udp_run (struct udp_pcb *pcb, HevTaskMutex *mutex,
                                   struct pbuf *p, const ip_addr_t *addr,
                                   u16_t port, const ip_addr_t *client_addr,
                                   u16_t client_port);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_DIRECT_CONNECTOR_H__ */
