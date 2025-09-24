#ifndef __HEV_CHNROUTES_MANAGER_H__
#define __HEV_CHNROUTES_MANAGER_H__

#include <lwip/ip_addr.h> // For ip_addr_t
#include <stdint.h>       // For uint32_t

#ifdef __cplusplus
extern "C" {
#endif

// Represents an IP address range (start and end IP for both IPv4 and IPv6)
typedef struct {
    ip_addr_t start_ip;
    ip_addr_t end_ip;
} HevIPRange;

/**
 * hev_chnroutes_manager_init:
 * @chnroutes_file_path: Path to the chnroutes data file.
 *
 * Initializes the chnroutes manager by loading IP ranges from the specified file.
 * The file should contain one CIDR block per line (e.g., "1.0.1.0/24").
 * Supports both IPv4 and IPv6 CIDRs.
 *
 * Returns: 0 on success, -1 on failure (e.g., file not found, parse error).
 */
int hev_chnroutes_manager_init(const char *chnroutes_file_path);

/**
 * hev_chnroutes_manager_fini:
 *
 * Frees resources used by the chnroutes manager.
 */
void hev_chnroutes_manager_fini(void);

/**
 * hev_chnroutes_manager_is_domestic:
 * @ip_addr: The IP address to check.
 *
 * Checks if the given IP address falls within the loaded chnroutes ranges.
 *
 * Returns: 1 if domestic, 0 if not, -1 if manager not initialized or error.
 */
int hev_chnroutes_manager_is_domestic(const ip_addr_t *ip_addr);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_CHNROUTES_MANAGER_H__ */
