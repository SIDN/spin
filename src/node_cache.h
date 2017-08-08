#ifndef SPIN_NODE_CACHE_H
#define SPIN_NODE_CACHE_H 1

#include "pkt_info.h"
#include "tree.h"
#include "arp.h"
#include "node_names.h"

#include <stdint.h>

typedef struct {
    int id;
    // note: ip's are in a 17-byte format (family + ip, padded with 12 zeroes in case of ipv4)
    // they are stored in the keys, data is empty
    tree_t* ips;
    // domains in string format, stored in the tree keys, with data empty
    tree_t* domains;
    // can be null
    char* name;
    // can be null
    char* mac;
    // at some point we may want to clean up stuff
    uint32_t last_seen;
} node_t;

node_t* node_create(int id);
void node_destroy(node_t* node);
node_t* node_clone(node_t* node);

void node_add_ip(node_t* node, uint8_t* ip);
void node_add_domain(node_t* node, char* domain);
void node_set_mac(node_t* node, char* mac);
void node_set_name(node_t* node, char* name);
void node_set_last_seen(node_t* node, uint32_t lastg_seen);

int node_shares_element(node_t* node, node_t* othernode);
void node_merge(node_t* dest, node_t* src);

void node_print(node_t* node);
unsigned int node2json(node_t* node, buffer_t* json_buf);

#define MAX_NODES 2048

typedef struct {
    // this tree holds the actual memory structure, indexed by their id
    tree_t* nodes;
    // this is a non-memory tree, indexed by the ip addresses
    tree_t* ip_refs;
    // keep a counter for new ids
    int available_id;
    // arp cache for mac lookups
    arp_table_t* arp_table;
    // names list as read from config files
    node_names_t* names;
} node_cache_t;

node_cache_t* node_cache_create(void);
void node_cache_destroy(node_cache_t* node_cache);

void node_cache_print(node_cache_t* node_cache);

void node_cache_add_ip_info(node_cache_t* node_cache, uint8_t* ip, uint32_t timestamp);
void node_cache_add_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info, uint32_t timestamp);
void node_cache_add_dns_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp);

/**
 * this takes ownership of the given node pointer, do not use or free after
 */
void node_cache_add_node(node_cache_t* node_cache, node_t* node);

node_t* node_cache_find_by_ip(node_cache_t* node_cache, size_t key_size, uint8_t* ip);
node_t* node_cache_find_by_domain(node_cache_t* node_cache, char* dname);
node_t* node_cache_find_by_id(node_cache_t* node_cache, int node_id);


/* convert pkt_info data to json using node information from the given node cache */


unsigned int pkt_info2json(node_cache_t* node_cache, pkt_info_t* pkt_info, buffer_t* json_buf);

typedef struct {
    int packet_count;
    int payload_size;
} flow_data_t;

typedef struct {
    // the tree is keyed by the first 38 bytes of a pktinfo,
    // and the data is the flow_data from above
    tree_t* flows;
    uint32_t timestamp;
    unsigned int total_size;
    unsigned int total_count;
} flow_list_t;

flow_list_t* flow_list_create(uint32_t timestamp);
void flow_list_destroy(flow_list_t* flow_list);
void flow_list_add_pktinfo(flow_list_t* flow_list, pkt_info_t* pkt_info);
int flow_list_should_send(flow_list_t* flow_list, uint32_t timestamp);
void flow_list_clear(flow_list_t* flow_list, uint32_t timestamp);
int flow_list_empty(flow_list_t* flow_list);
unsigned int flow_list2json(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf);

unsigned int create_traffic_command(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf, uint32_t timestamp);



#endif // SPIN_NODE_CACHE_H
