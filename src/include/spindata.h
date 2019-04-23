#include <cJSON.h>

typedef cJSON *spin_data;

char *spin_data_serialize(spin_data sd);
void spin_data_delete(spin_data sd);

spin_data ipar_json(tree_t *iptree);
spin_data node_json(node_t* node);
spin_data pkt_info_json(node_cache_t* node_cache, pkt_info_t* pkt_info);
spin_data dns_query_pkt_info_json(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt_info);
spin_data create_traffic_json (node_cache_t* node_cache, flow_list_t* flow_list, uint32_t timestamp);
