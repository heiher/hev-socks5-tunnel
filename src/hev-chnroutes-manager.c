#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

#include <lwip/ip_addr.h>
#include <lwip/ip4_addr.h>
#include <lwip/ip6_addr.h>
#include <lwip/init.h> // Added for general lwip definitions
#include <lwip/netif.h> // Added for ip_addr_is_any/loopback if needed

#include <hev-memory-allocator.h>
#include <hev-logger.h>

#include <hev-chnroutes-manager.h>

#define MAX_CIDR_LINE_LEN 64

static HevIPRange *chnroutes_ranges = NULL;
static size_t num_chnroutes_ranges = 0;
static int is_initialized = 0;

static int
compare_ip_ranges (const void *a, const void *b)
{
    const HevIPRange *range_a = (const HevIPRange *)a;
    const HevIPRange *range_b = (const HevIPRange *)b;

    if (IP_IS_V4_VAL(range_a->start_ip) && IP_IS_V4_VAL(range_b->start_ip)) {
        uint32_t ip_a = ntohl(ip_addr_get_ip4_u32(&range_a->start_ip));
        uint32_t ip_b = ntohl(ip_addr_get_ip4_u32(&range_b->start_ip));
        if (ip_a < ip_b) return -1;
        if (ip_a > ip_b) return 1;
    } else if (IP_IS_V6_VAL(range_a->start_ip) && IP_IS_V6_VAL(range_b->start_ip)) {
        return memcmp(&range_a->start_ip.u_addr.ip6, &range_b->start_ip.u_addr.ip6, sizeof(struct ip6_addr));
    } else {
        // Mixed IPv4/IPv6, IPv4 comes before IPv6
        if (IP_IS_V4_VAL(range_a->start_ip)) return -1;
        if (IP_IS_V4_VAL(range_b->start_ip)) return 1;
    }
    return 0;
}

static int
parse_cidr (const char *cidr_str, HevIPRange *range)
{
    char ip_str[MAX_CIDR_LINE_LEN];
    char *slash;
    int prefix_len;

    strncpy (ip_str, cidr_str, sizeof(ip_str) - 1);
    ip_str[sizeof(ip_str) - 1] = '\0';

    slash = strchr (ip_str, '/');
    if (!slash) {
        LOG_W ("Chnroutes: Invalid CIDR format: %s", cidr_str);
        return -1;
    }
    *slash = '\0';
    prefix_len = atoi (slash + 1);

    if (inet_pton (AF_INET, ip_str, &range->start_ip.u_addr.ip4.addr) == 1) {
        IP_SET_TYPE(&range->start_ip, IPADDR_TYPE_V4);
        if (prefix_len < 0 || prefix_len > 32) {
            LOG_W ("Chnroutes: Invalid IPv4 prefix length: %s", cidr_str);
            return -1;
        }
        uint32_t mask = htonl (~((1ULL << (32 - prefix_len)) - 1));
        range->start_ip.u_addr.ip4.addr &= mask;
        range->end_ip.u_addr.ip4.addr = range->start_ip.u_addr.ip4.addr | ~mask;
        IP_SET_TYPE(&range->end_ip, IPADDR_TYPE_V4);
    } else if (inet_pton (AF_INET6, ip_str, &range->start_ip.u_addr.ip6.addr) == 1) {
        IP_SET_TYPE(&range->start_ip, IPADDR_TYPE_V6);
        if (prefix_len < 0 || prefix_len > 128) {
            LOG_W ("Chnroutes: Invalid IPv6 prefix length: %s", cidr_str);
            return -1;
        }
        // Simplified IPv6 CIDR calculation
        range->end_ip = range->start_ip;

        int p = prefix_len;
        for (int i = 0; i < 4; i++) {
            uint32_t mask_h; // host byte order mask
            if (p >= 32) {
                mask_h = 0xFFFFFFFF;
                p -= 32;
            } else if (p > 0) {
                mask_h = 0xFFFFFFFFU << (32 - p);
                p = 0;
            } else {
                mask_h = 0;
            }

            uint32_t mask_n = htonl(mask_h); // network byte order mask
            range->start_ip.u_addr.ip6.addr[i] &= mask_n;
            range->end_ip.u_addr.ip6.addr[i] |= ~mask_n;
        }
        IP_SET_TYPE(&range->end_ip, IPADDR_TYPE_V6);
    } else {
        LOG_W ("Chnroutes: Invalid IP address in CIDR: %s", cidr_str);
        return -1;
    }

    return 0;
}

int
hev_chnroutes_manager_init (const char *chnroutes_file_path)
{
    FILE *fp = NULL;
    char line[MAX_CIDR_LINE_LEN];
    HevIPRange *temp_ranges = NULL;
    size_t temp_count = 0;
    int res = -1;

    if (is_initialized) {
        LOG_W ("Chnroutes manager already initialized.");
        return 0;
    }

    fp = fopen (chnroutes_file_path, "r");
    if (!fp) {
        LOG_E ("Chnroutes: Failed to open file %s: %s", chnroutes_file_path, strerror (errno));
        goto exit;
    }

    temp_ranges = hev_malloc (sizeof (HevIPRange) * 1024); // Initial allocation
    if (!temp_ranges) {
        LOG_E ("Chnroutes: Failed to allocate memory for ranges.");
        goto exit;
    }
    size_t allocated_size = 1024;

    while (fgets (line, sizeof (line), fp)) {
        // Remove newline character
        line[strcspn (line, "\n")] = '\0';
        if (strlen (line) == 0) continue;

        if (temp_count >= allocated_size) {
            allocated_size *= 2;
            HevIPRange *new_ptr = hev_realloc (temp_ranges, sizeof (HevIPRange) * allocated_size);
            if (!new_ptr) {
                LOG_E ("Chnroutes: Failed to reallocate memory for ranges.");
                goto exit;
            }
            temp_ranges = new_ptr;
        }

        if (parse_cidr (line, &temp_ranges[temp_count]) == 0) {
            temp_count++;
        }
    }

    if (temp_count == 0) {
        LOG_W ("Chnroutes: No valid CIDR entries found in file %s.", chnroutes_file_path);
        goto exit;
    }

    chnroutes_ranges = hev_realloc (temp_ranges, sizeof (HevIPRange) * temp_count);
    if (!chnroutes_ranges) {
        LOG_E ("Chnroutes: Failed to finalize memory allocation.");
        chnroutes_ranges = temp_ranges; // Keep old pointer for freeing
        goto exit;
    }
    num_chnroutes_ranges = temp_count;

    qsort (chnroutes_ranges, num_chnroutes_ranges, sizeof (HevIPRange), compare_ip_ranges);

    LOG_I ("Chnroutes: Loaded %zu domestic IP ranges.", num_chnroutes_ranges);

    is_initialized = 1;
    res = 0;

exit:
    if (fp) fclose (fp);
    if (res != 0 && temp_ranges) hev_free (temp_ranges);
    return res;
}

void
hev_chnroutes_manager_fini (void)
{
    if (chnroutes_ranges) {
        hev_free (chnroutes_ranges);
        chnroutes_ranges = NULL;
        num_chnroutes_ranges = 0;
    }
    is_initialized = 0;
}

int
hev_chnroutes_manager_is_domestic (const ip_addr_t *ip_addr)
{
    if (!is_initialized) {
        LOG_W ("Chnroutes manager not initialized.");
        return -1;
    }

    if (ip_addr_isany(ip_addr) || ip_addr_isloopback(ip_addr)) {
        return 0; // Not considered domestic for routing purposes
    }

    // Binary search
    int low = 0;
    int high = num_chnroutes_ranges - 1;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        const HevIPRange *current_range = &chnroutes_ranges[mid];

        if (IP_IS_V4_VAL(*ip_addr) && IP_IS_V4_VAL(current_range->start_ip)) {
            uint32_t target_ip_host = ntohl(ip_addr_get_ip4_u32(ip_addr));
            uint32_t start_ip_host = ntohl(ip_addr_get_ip4_u32(&current_range->start_ip));
            uint32_t end_ip_host = ntohl(ip_addr_get_ip4_u32(&current_range->end_ip));

            if (target_ip_host >= start_ip_host && target_ip_host <= end_ip_host) {
                return 1; // Found in range
            } else if (target_ip_host < start_ip_host) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        } else if (IP_IS_V6_VAL(*ip_addr) && IP_IS_V6_VAL(current_range->start_ip)) {
            // IPv6 comparison
            int cmp_start = ip6_addr_cmp(&ip_addr->u_addr.ip6, &current_range->start_ip.u_addr.ip6);
            int cmp_end = ip6_addr_cmp(&ip_addr->u_addr.ip6, &current_range->end_ip.u_addr.ip6);

            if (cmp_start >= 0 && cmp_end <= 0) {
                return 1; // Found in range
            } else if (cmp_start < 0) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        } else {
            // Mismatched IP versions, skip or handle as not domestic
            // For binary search, we need to ensure consistent ordering.
            // Our compare_ip_ranges puts IPv4 before IPv6.
            if (IP_IS_V4_VAL(*ip_addr)) { // Target is IPv4, current range is IPv6
                high = mid - 1; // Target must be before this IPv6 range
            } else {
                low = mid + 1;
            }
        }
    }

    return 0; // Not found
}
