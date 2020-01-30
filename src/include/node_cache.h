#ifndef SPIN_NODE_CACHE_H
#define SPIN_NODE_CACHE_H 1

#include "util.h"
#include "spin_list.h"
#include "pkt_info.h"
#include "tree.h"
#include "arp.h"
#include "node_names.h"

#include <stdint.h>

typedef struct {
    int     dvf_blocked;
    int     dvf_packets;
    int     dvf_bytes;
    uint32_t dvf_lastseen;
    int     dvf_idleperiods;
    int     dvf_activelastperiod;
} devflow_t;

typedef struct {
    int     dst_node_id;
    int     dst_port;
    // Should we differentiate on source port too?
    // that makes ephemeral port use a problem, but
    // would result in better DOTS matching...
    int     icmp_type;
} devflow_key_t;

typedef struct {
    tree_t *dv_flowtree;
    int dv_nflows;
} device_t;

typedef struct {
    int id;
    // note: ip's are in a sizeof(ip_t)-byte format (family + ip, padded with 12 zeroes in case of ipv4)
    // they are stored in the keys, data is empty
    tree_t* ips;
    // domains in string format, stored in the tree keys, with data empty
    tree_t* domains;
    // can be null
    char* name;
    // can be null
    char* mac;
    // some additional info about this node
    int is_onlist[N_IPLIST];
    // at some point we may want to clean up stuff, so keep track of
    // when we last saw it
    uint32_t last_seen;
    // and for publication purposes also if it changed
    uint8_t modified;
    // and for keeping if in blocking
    uint32_t persistent;
    // and references of flows
    uint32_t references;
    device_t* device;
} node_t;

#define is_blocked is_onlist[IPLIST_BLOCK]
#define is_allowed is_onlist[IPLIST_ALLOW]

typedef void (*modfunc)(node_t *);

node_t* node_create(int id);
/*
void node_destroy(node_t* node);
node_t* node_clone(node_t* node);
*/

void node_add_ip(node_t* node, ip_t* ip);
void node_add_domain(node_t* node, char* domain);

void node_set_name(node_t* node, char* name);
void node_set_mac(node_t* node, char* mac);
void node_set_modified(node_t *node, uint32_t now);
/*
void node_set_blocked(node_t* node, int blocked);
void node_set_excepted(node_t* node, int excepted);
void node_set_last_seen(node_t* node, uint32_t lastg_seen);

int node_shares_element(node_t* node, node_t* othernode);
*/
/*
 * Merge two nodes;
 * Add all IP addresses and domain names that are in src to dest
 * Set the last_seen value to the highest of the two
 * If either of them have non-zero is_blocked or is_excepted, copy that
 * value
 * If name of dest is not set, set it to name of src
 * If mac of dest is not set, set it to mac of src
void node_merge(node_t* dest, node_t* src);

void node_print(node_t* node);
unsigned int node2json(node_t* node, buffer_t* json_buf);
 */

#define MAX_NODES 2048

typedef struct {
    // this tree holds the actual memory structure, indexed by their id
    tree_t* nodes;
    // this is a non-memory tree, indexed by the ip addresses
    // unused, TODO, HvS
    tree_t* ip_refs;
    tree_t* domain_refs;
    tree_t* mac_refs;
    // keep a counter for new ids
    int available_id;
    // arp cache for mac lookups
    arp_table_t* arp_table;
    // names list as read from config files
    node_names_t* names;
} node_cache_t;

node_cache_t* node_cache_create(void);
void node_cache_destroy(node_cache_t* node_cache);

typedef void (*cleanfunc)(node_cache_t *, node_t*, void *);

void node_callback_new(node_cache_t *node_cache, modfunc);
void node_callback_devices(node_cache_t *node_cache, cleanfunc, void *);

void node_cache_update_arp(node_cache_t *node_cache, uint32_t timestamp);
void node_cache_print(node_cache_t* node_cache);
/*

void node_cache_add_ip_info(node_cache_t* node_cache, ip_t* ip, uint32_t timestamp);
 */
void node_cache_add_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info, uint32_t timestamp);
void node_cache_add_dns_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp);
void node_cache_add_dns_query_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp);

void xnode_add_ip(node_cache_t *node_cache, node_t* node, ip_t* ip);

/**
 * this takes ownership of the given node pointer, do not use or free after!
 * Returns 1 if the added node is new, 0 if not
 */
int node_cache_add_node(node_cache_t* node_cache, node_t* node);

node_t* node_cache_find_by_ip(node_cache_t* node_cache, size_t key_size, ip_t* ip);
/**
 * Note: currently the cache stores domain names by their string representation, so convert
 * if you are looking for a node by its wireformat domain
 */
node_t* node_cache_find_by_domain(node_cache_t* node_cache, char* dname);
node_t* node_cache_find_by_mac(node_cache_t* node_cache, char* macaddr);
node_t* node_cache_find_by_id(node_cache_t* node_cache, int node_id);

/**
 * Remove all entries from the node cache that have a last_seen value
 * that is smaller than the 'older_than' argument
 * NOTE: this method will be enhanced shortly with an optional callback
 * (so that we can, say, 'unpublish' cleaned-up nodes)
 */
void node_cache_clean(node_cache_t* node_cache, uint32_t older_than);

/* convert pkt_info data to json using node information from the given node cache */


unsigned int pkt_info2json(node_cache_t* node_cache, pkt_info_t* pkt_info, buffer_t* json_buf);
unsigned int dns_query_pkt_info2json(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt_info, buffer_t* json_buf);

typedef struct {
    uint64_t packet_count;
    uint64_t payload_size;
} flow_data_t;

typedef struct {
    // the tree is keyed by the first 38 bytes of a pktinfo,
    // and the data is the flow_data from above
    tree_t* flows;
    uint32_t timestamp;
    uint64_t total_size;
    uint64_t total_count;
} flow_list_t;

flow_list_t* flow_list_create(uint32_t timestamp);
void flow_list_destroy(flow_list_t* flow_list);
void flow_list_add_pktinfo(flow_list_t* flow_list, pkt_info_t* pkt_info);
int flow_list_should_send(flow_list_t* flow_list, uint32_t timestamp);
void flow_list_clear(flow_list_t* flow_list, uint32_t timestamp);
int flow_list_empty(flow_list_t* flow_list);
/*
unsigned int flow_list2json(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf);

unsigned int create_traffic_command(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf, uint32_t timestamp);

*/

void node_cache_update_iplist_node(node_cache_t* node_cache, int listid, int addrem, int node_id);

/*
 * 'Upgrades' the node to a device, and update/add the corresponding datastructures
 *
 * The node must not have the device status yet (e.g. the device field must be null)
 */
void makedevice(node_t *node);
void merge_nodes(node_cache_t *node_cache, node_t* src_node, node_t* dest_node);

void cache_tree_add_mac(node_cache_t *node_cache, node_t* node, char* mac);
void cache_tree_remove_mac(node_cache_t *node_cache, char* mac);

int cmp_flow_keys(size_t size_a, const void* a, size_t size_b, const void* b);


#endif // SPIN_NODE_CACHE_H
