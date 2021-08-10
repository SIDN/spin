#ifndef SPIN_DATA_H
#define SPIN_DATA_H 1
#include "spindata_type.h"
#include "node_cache.h"
#include "tree.h"

spin_data spin_data_nodes_merged(int node1, int node2);
spin_data spin_data_node_deleted(int node);
spin_data spin_data_ipar(tree_t *iptree);
spin_data spin_data_node(node_t* node);
spin_data spin_data_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info);
spin_data spin_data_dns_query_pkt_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt_info);
spin_data spin_data_create_mqtt_command(const char* command, char* argument, spin_data result);
spin_data spin_data_create_traffic(node_cache_t* node_cache, flow_list_t* flow_list, uint32_t timestamp);
spin_data spin_data_nodepairtree(tree_t* tree);
spin_data spin_data_devicelist(node_cache_t *node_cache);
spin_data spin_data_flowlist(node_t *node);
#endif
