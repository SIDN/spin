#include "spindata.h"

int cmp_flow_keys(size_t size_a, const void* a, size_t size_b, const void* b);
void spinhook_nodesmerged(node_cache_t *node_cache, node_t *dest_node, node_t *src_node);
void spinhook_nodedeleted(node_cache_t *node_cache, node_t *node);
void spinhook_traffic(node_cache_t *node_cache, node_t *src_node, node_t *dest_node, int packetcnt, int packetbytes, uint32_t timestamp, int, int);
void spinhook_block_dev_node_flow(device_t *dev, node_t *node, int blocked);
void spinhook_clean(node_cache_t *node_cache);
