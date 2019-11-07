#include <stddef.h>

#include "node_cache.h"

/*
 * If the specified MAC address is found in the node cache, use the extsrc
 * facility to tell spind this is a local device.
 */
void mark_local_device(int fd, node_cache_t *node_cache, const uint8_t *mac,
    const uint8_t *ip, uint8_t family);

/*
 * Add the specified MAC address to the node cache. The result is that packets
 * with a matching MAC address will be considered a ``local'' device.
 */
void x_node_cache_add_mac_uint8t(node_cache_t *node_cache, const uint8_t *mac);
void x_node_cache_add_mac_macstr(node_cache_t *node_cache, char *mac);
