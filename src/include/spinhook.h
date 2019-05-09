#include "spindata.h"

void spinhook_nodesmerged(node_cache_t *node_cache, node_t *dest_node, node_t *src_node);
void spinhook_makedevice(node_t *node);
void spinhook_traffic(node_t *src_node, node_t *dest_node, int packetcnt, int packetbytes);
void spinhook_clean(node_cache_t *node_cache);

spin_data spinhook_json(spin_data sd);

void spinhook_init();
