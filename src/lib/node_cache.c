
#include "spin_log.h"

#include <assert.h>
#include <arpa/inet.h>
#include "util.h"
#include "spin_list.h"
#include "node_cache.h"
#include "netlink_commands.h"
#include "spin_cfg.h"
#include "statistics.h"

STAT_MODULE(node_cache)

static int node_cache_add_node(node_cache_t* node_cache, node_t* node);

STAT_COUNTER(nodes, "nodes", STAT_TOTAL);

static node_t*
node_create(int id) {
    int i;

    STAT_VALUE(nodes, 1);
    node_t* node = (node_t*) malloc(sizeof(node_t));
    node->id = id;
    node->ips = tree_create(cmp_ips);
    node->domains = tree_create(cmp_domains);
    node->name = NULL;
    node->mac = NULL;
    for (i=0;i<N_IPLIST;i++) {
        node->is_onlist[i] = 0;
    }
    node->last_seen = 0;
    return node;
}

static void
node_destroy(node_t* node) {

    STAT_VALUE(nodes, -1);
    tree_destroy(node->ips);
    node->ips = NULL;
    tree_destroy(node->domains);
    node->domains = NULL;
    if (node->mac) {
        free(node->mac);
        node->mac = NULL;
    }
    if (node->name) {
        free(node->name);
        node->name = NULL;
    }
    free(node);
}

#ifdef notdef

/*
 * Seems unused, HVS
 */
node_t* node_clone(node_t* node) {
    int i;

    node_t* new = node_create(node->id);
    tree_entry_t *cur;
    if (node->mac) {
        node_set_mac(new, node->mac);
    }
    if (node->name) {
        node_set_name(new, node->name);
    }
    for (i=0;i<N_IPLIST;i++) {
        new->is_onlist[i] = node->is_onlist[i];
    }
    new->last_seen = node->last_seen;
    cur = tree_first(node->ips);
    while (cur != NULL) {
        tree_add(new->ips, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    cur = tree_first(node->domains);
    while (cur != NULL) {
        tree_add(new->domains, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    return new;
}
#endif

static void
node_add_ip(node_t* node, ip_t* ip) {
    STAT_COUNTER(ctr, add-ip, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    tree_add(node->ips, sizeof(ip_t), ip, 0, NULL, 1);
}

static void
node_add_domain(node_t* node, char* domain) {
    STAT_COUNTER(ctr, add-domain, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    tree_add(node->domains, strlen(domain) + 1, domain, 0, NULL, 1);
}

static void
node_set_mac(node_t* node, char* mac) {
    STAT_COUNTER(ctr, set-mac, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    if (mac == NULL) {
        return;
    }
    if (node->mac != NULL) {
        free(node->mac);
    }
    node->mac = strndup(mac, 18);
}

void
node_set_name(node_t* node, char* name) {
    STAT_COUNTER(ctr, set-name, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    if (name == NULL) {
        return;
    }
    if (node->name != NULL) {
        free(node->name);
    }
    node->name = strndup(name, 128);
}

static void
node_set_last_seen(node_t* node, uint32_t last_seen) {
    node->last_seen = last_seen;
}

static void ip_key2str(char* buf, size_t buf_len, const uint8_t* ip_key_data) {
    int family = (int)ip_key_data[0];
    size_t offset;
    if (family == AF_INET) {
        offset = 13;
    } else {
        offset = 1;
    }
    ntop(family, buf, (const uint8_t*)&ip_key_data[offset], buf_len);
}

static int
node_shares_element(node_t* node, node_t* othernode) {
    tree_entry_t* cur_me;

    //spin_log(LOG_DEBUG, "[XX] ips  at %p\n", node->ips);
    //fflush(stdout);
    if (node->mac != NULL && othernode->mac != NULL) {
        if (strcmp(node->mac, othernode->mac) == 0) {
            spin_log(LOG_DEBUG, "[MERGE] Nodes %d and %d share mac address %s\n", node->id, othernode->id, node->mac);
            return 1;
        }
    }

    cur_me = tree_first(node->ips);
    while (cur_me != NULL) {
        if (tree_find(othernode->ips, cur_me->key_size, cur_me->key) != NULL) {
            char ip_str[256];
            ip_key2str(ip_str, 256, cur_me->key);
            spin_log(LOG_DEBUG, "[MERGE] Nodes %d and %d share IP address %s\n", node->id, othernode->id, ip_str);
            return 1;
        }
        cur_me = tree_next(cur_me);
    }

    cur_me = tree_first(node->domains);
    while (cur_me != NULL) {
        if (tree_find(othernode->domains, cur_me->key_size, cur_me->key) != NULL) {
            spin_log(LOG_DEBUG, "[MERGE3] Nodes %d and %d share domain name %s\n", node->id, othernode->id, cur_me->key);
            return 1;
        }
        cur_me = tree_next(cur_me);
    }

    return 0;
}

static void
node_merge(node_t* dest, node_t* src) {
    tree_entry_t* cur;
    int i;
    STAT_COUNTER(ip_size, ip-tree-size, STAT_MAX);
    STAT_COUNTER(domain_size, domain-tree-size, STAT_MAX);

    if (dest->name == NULL) {
        node_set_name(dest, src->name);
    }
    if (dest->mac == NULL) {
        node_set_mac(dest, src->mac);
    }
    if (dest->last_seen < src->last_seen) {
        dest->last_seen = src->last_seen;
    }
    // When merging nodes, set is_onlist[] entries to 1 if either
    // of them were not 0
    for (i=0;i<N_IPLIST;i++) {
        dest->is_onlist[i] |= src->is_onlist[i];
    }

    cur = tree_first(src->ips);
    while (cur != NULL) {
        tree_add(dest->ips, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    STAT_VALUE(ip_size, tree_size(dest->ips));

    cur = tree_first(src->domains);
    while (cur != NULL) {
        tree_add(dest->domains, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    STAT_VALUE(domain_size, tree_size(dest->domains));
}

static void
node_print(node_t* node) {
    tree_entry_t* cur;
    int fam;
    unsigned char* keyp;
    char str[512];

    spin_log(LOG_DEBUG, "[NODE] id: %d\n", node->id);
    if (node->name != NULL) {
        spin_log(LOG_DEBUG, "       name: %s\n", node->name);
    }
    if (node->mac != NULL) {
        spin_log(LOG_DEBUG, "      mac: %s\n", node->mac);
    }
    spin_log(LOG_DEBUG, "      ips:\n");
    cur = tree_first(node->ips);
    while (cur != NULL) {
        assert(cur->key_size == sizeof(ip_t));
        keyp = (unsigned char*) cur->key;
        fam = (int)keyp[0];
        if (fam == AF_INET) {
            ntop((int)keyp[0], str, (const uint8_t*)&keyp[13], 1024);
        } else {
            ntop((int)keyp[0], str, (const uint8_t*)&keyp[1], 1024);
        }
        spin_log(LOG_DEBUG, "        %s\n", str);
        cur = tree_next(cur);
    }
    spin_log(LOG_DEBUG, "      domains:\n");
    cur = tree_first(node->domains);
    while (cur != NULL) {
        spin_log(LOG_DEBUG, "        %s\n", (unsigned char*)cur->key);
        cur = tree_next(cur);
    }
}

static unsigned int
node2json(node_t* node, buffer_t* json_buf) {
    unsigned int s = 0;
    tree_entry_t* cur;
    char ip_str[INET6_ADDRSTRLEN];

    buffer_write(json_buf, "{ \"id\": %d, ", node->id);
    if (node->name != NULL) {
        buffer_write(json_buf, " \"name\": \"%s\", ", node->name);
    }
    if (node->mac != NULL) {
        buffer_write(json_buf, " \"mac\": \"%s\", ", node->mac);
    }
    if (node->is_blocked) {
        buffer_write(json_buf, " \"is_blocked\": \"true\", ");
    }
    if (node->is_allowed) {
        buffer_write(json_buf, " \"is_excepted\": \"true\", ");
    }
    buffer_write(json_buf, " \"lastseen\": %u, ", node->last_seen);

    buffer_write(json_buf, " \"ips\": [ ");
    cur = tree_first(node->ips);
    while (cur != NULL) {
        buffer_write(json_buf, "\"");
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        buffer_write(json_buf, ip_str);
        //s += spin_ntop(dest + s, (uint8_t*)cur->key, max_len - s);
        buffer_write(json_buf, "\"");
        cur = tree_next(cur);
        if (cur != NULL) {
            buffer_write(json_buf, ", ");
        }
    }
    buffer_write(json_buf, " ], ");

    buffer_write(json_buf, " \"domains\": [ ");
    cur = tree_first(node->domains);
    while (cur != NULL) {
        buffer_write(json_buf, "\"%s\"", (uint8_t*)cur->key);
        cur = tree_next(cur);
        if (cur != NULL) {
            buffer_write(json_buf, ", ");
        }
    }
    buffer_write(json_buf, " ] }");

    return s;
}

/*
 * Create and destroy node_cache
 * As far as I can see only done once in current node, only one node cache in use
 *
 * Perhaps TODO
 */

node_cache_t*
node_cache_create() {
    node_cache_t* node_cache = (node_cache_t*)malloc(sizeof(node_cache_t));
    node_cache->nodes = tree_create(cmp_ints);

    node_cache->ip_refs = tree_create(cmp_ips);

    node_cache->available_id = 1;

    node_cache->arp_table = arp_table_create();
    node_cache->names = node_names_create();
    node_names_read_dhcpconfig(node_cache->names, "/etc/config/dhcp");
    // can/should we regularly call this again?
    node_names_read_dhcpleases(node_cache->names, "/var/dhcp.leases");
    node_names_read_userconfig(node_cache->names, "/etc/spin/names.conf");
    return node_cache;
}

void
node_cache_destroy(node_cache_t* node_cache) {
    node_t* cur_node;
    tree_entry_t* cur = tree_first(node_cache->nodes);
    while (cur != NULL) {
        cur_node = (node_t*)cur->data;
        node_destroy(cur_node);
        cur->data = NULL;
        cur = tree_next(cur);
    }
    tree_destroy(node_cache->nodes);
    tree_destroy(node_cache->ip_refs);
    arp_table_destroy(node_cache->arp_table);
    node_names_destroy(node_cache->names);
    free(node_cache);
}

static void
node_cache_print(node_cache_t* node_cache) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* cur_node;
    spin_log(LOG_DEBUG, "[node cache]\n");
    while (cur != NULL) {
        cur_node = (node_t*) cur->data;
        node_print(cur_node);
        cur = tree_next(cur);
    }
    spin_log(LOG_DEBUG, "[end of node cache]\n");

}

node_t* node_cache_find_by_ip(node_cache_t* node_cache, size_t key_size, ip_t* ip) {
    // TODO: this is very inefficient; we should add a second tree for ip searching
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* node;
    STAT_COUNTER(ctr, find-by-ip, STAT_TOTAL);
    int loopcnt=0;

    while (cur != NULL) {
        loopcnt++;
        node = (node_t*)cur->data;
        // can we use a node_has_ip?
        if (tree_find(node->ips, sizeof(ip_t), ip) != NULL) {
            STAT_VALUE(ctr, loopcnt);
            return node;
        }
        cur = tree_next(cur);
    }
    STAT_VALUE(ctr, loopcnt);
    return NULL;
}

node_t* node_cache_find_by_domain(node_cache_t* node_cache, char* dname) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* node;
    STAT_COUNTER(ctr, find-by-domain, STAT_TOTAL);
    int loopcnt=0;

    while (cur != NULL) {
        loopcnt++;
        node = (node_t*)cur->data;
        // can we use a node_has_domain?
        if (tree_find(node->domains, strlen(dname) + 1, dname) != NULL) {
            STAT_VALUE(ctr, loopcnt);
            return node;
        }
        cur = tree_next(cur);
    }
    STAT_VALUE(ctr, loopcnt);
    return NULL;
}

node_t* node_cache_find_by_id(node_cache_t* node_cache, int node_id) {
    int my_id = node_id;
    tree_entry_t* cur = tree_find(node_cache->nodes, sizeof(node_id), &my_id);
    if (cur == NULL) {
        return NULL;
    } else {
        return (node_t*)cur->data;
    }
}

static int
node_cache_get_new_id(node_cache_t* node_cache) {
    int nextid;
    STAT_COUNTER(nnodes, number-nodes, STAT_MAX);

    // just incremental for now
    nextid = node_cache->available_id++;
    STAT_VALUE(nnodes, node_cache->available_id);
}

static void
add_mac_and_name(node_cache_t* node_cache, node_t* node, ip_t* ip) {
    char* mac = arp_table_find_by_ip(node_cache->arp_table, ip);
    char* name;
    if (!mac) {
        // todo: incorporate this in standard lookup?
        arp_table_read(node_cache->arp_table);
        mac = arp_table_find_by_ip(node_cache->arp_table, ip);
    }
    if (mac) {
        // spin_log(LOG_DEBUG, "[XX] mac found: %s\n", mac);
        node_set_mac(node, mac);
        name = node_names_find_mac(node_cache->names, mac);
        if (name != NULL) {
            node_set_name(node, name);
        }
    } else {
        // spin_log(LOG_DEBUG, "[XX] mac not found\n");
        name = node_names_find_ip(node_cache->names, ip);
        if (name != NULL) {
            node_set_name(node, name);
        }
    }
    // spin_log(LOG_DEBUG, "[XX] mac at %p\n", node->mac);
}

static void
node_cache_add_ip_info(node_cache_t* node_cache, ip_t* ip, uint32_t timestamp) {
    // todo: add an search-by-ip tree and don't do anything if we
    // have this one already? (do set mac if now known,
    // and update last_seen)
    node_t* node = node_create(0);
    char* name;
    int new;
    STAT_COUNTER(ctr, add-ip-info, STAT_TOTAL);

    node_set_last_seen(node, timestamp);
    add_mac_and_name(node_cache, node, ip);
    node_add_ip(node, ip);
    new = node_cache_add_node(node_cache, node);
    STAT_VALUE(ctr, new);
    if (new == 1) {
        // It was new; reread the DHCP leases table, and set the name if it wasn't set yet
        node_names_read_dhcpleases(node_cache->names, "/var/dhcp.leases");
        if (node->mac && !node->name) {
            name = node_names_find_mac(node_cache->names, node->mac);
            if (name != NULL) {
                node_set_name(node, name);
            }

        }
    }
}

void node_cache_add_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info, uint32_t timestamp) {
    ip_t ip;
    ip.family = pkt_info->family;
    memcpy(ip.addr, pkt_info->src_addr, 16);
    node_cache_add_ip_info(node_cache, &ip, timestamp);
    memcpy(ip.addr, pkt_info->dest_addr, 16);
    node_cache_add_ip_info(node_cache, &ip, timestamp);
}

void node_cache_add_dns_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp) {
    // should first see if we have a node with this ip or domain already
    char dname_str[512];
    ip_t ip;
    ip.family = dns_pkt->family;
    memcpy(ip.addr, dns_pkt->ip, 16);
    dns_dname2str(dname_str, dns_pkt->dname, 512);

    node_t* node = node_create(0);
    node_set_last_seen(node, timestamp);
    node_add_ip(node, &ip);
    node_add_domain(node, dname_str);
    add_mac_and_name(node_cache, node, &ip);
    node_cache_add_node(node_cache, node);
}

void node_cache_add_dns_query_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp) {
    // first see if we have a node with this ip or domain already
    char dname_str[512];
    ip_t ip;
    ip.family = dns_pkt->family;
    memcpy(ip.addr, dns_pkt->ip, 16);
    dns_dname2str(dname_str, dns_pkt->dname, 512);

    // add the node with the domain name; if it is not known
    // this will result in a 'node' with only the domain name
    node_t* node = node_create(0);
    node_set_last_seen(node, timestamp);
    node_add_domain(node, dname_str);
    node_cache_add_node(node_cache, node);

    // in this case, the dns_pkt's ip address is a separate node!
    // add it too if it does not exist
    node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    if (node == NULL) {
        node = node_create(0);
        node_set_last_seen(node, timestamp);
        node_add_ip(node, &ip);
        node_cache_add_node(node_cache, node);
    }
}

// return 0 if it existed/was merged
// return 1 if it was new
static int
node_cache_add_node(node_cache_t* node_cache, node_t* node) {
    int new_id, *new_id_mem;
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* tree_node;
    int node_found = 0;
    tree_entry_t* nxt;
    STAT_COUNTER(ctr, node-sharing, STAT_TOTAL);

    while (cur != NULL) {
        tree_node = (node_t*) cur->data;
        if (node_shares_element(node, tree_node)) {
            if (!node_found) {
                node_merge(tree_node, node);
                node_found = 1;
                // TODO: walk the rest of the cache too, we may need to merge more nodes now
                node_destroy(node);
                node = tree_node;
                cur = tree_next(cur);
            } else {
                // note: we don't need to restart the loop since we know we have not found
                // any mergable items so far

                // TODO: this need to signal that existing nodes have been merged
                // or will we ignore that and let time fix it?
                node_merge(node, tree_node);

                // clean up the tree
                nxt = tree_next(cur);
                node_destroy(tree_node);
                cur->data = NULL;
                tree_remove_entry(node_cache->nodes, cur);
                cur = nxt;
            }
        } else {
            cur = tree_next(cur);
        }
    }
    STAT_VALUE(ctr, node_found);
    if (node_found) {
        return 0;
    }
    // ok no elements at all, add as a new one
    new_id = node_cache_get_new_id(node_cache);
    new_id_mem = (int*) malloc(sizeof(new_id));
    memcpy(new_id_mem, &new_id, sizeof(new_id));
    node->id = new_id;

    tree_add(node_cache->nodes, sizeof(new_id), new_id_mem, sizeof(node_t), node, 0);
    return 1;
}

unsigned int
pkt_info2json(node_cache_t* node_cache, pkt_info_t* pkt_info, buffer_t* json_buf) {
    unsigned int s = 0;
    node_t* src_node;
    node_t* dest_node;
    ip_t ip;

    ip.family = pkt_info->family;
    memcpy(ip.addr, pkt_info->src_addr, 16);
    src_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    memcpy(ip.addr, pkt_info->dest_addr, 16);
    dest_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    if (src_node == NULL) {
        char pkt_str[1024];
        spin_log(LOG_ERR, "[XX] ERROR! src node not found in cache!\n");
        pktinfo2str(pkt_str, pkt_info, 1024);
        spin_log(LOG_DEBUG, "[XX] pktinfo: %s\n", pkt_str);
        spin_log(LOG_DEBUG, "[XX] node cache:\n");
        node_cache_print(node_cache);
        return 0;
    }
    if (dest_node == NULL) {
        char pkt_str[1024];
        spin_log(LOG_ERR, "[XX] ERROR! dest node not found in cache!\n");
        pktinfo2str(pkt_str, pkt_info, 1024);
        spin_log(LOG_DEBUG, "[XX] pktinfo: %s\n", pkt_str);
        spin_log(LOG_DEBUG, "[XX] node cache:\n");
        node_cache_print(node_cache);
        return 0;
    }
    assert(src_node != NULL);
    assert(dest_node != NULL);

    buffer_write(json_buf, "{ \"from\": ");
    s += node2json(src_node, json_buf);
    buffer_write(json_buf, ", \"to\": ");
    s += node2json(dest_node, json_buf);
    buffer_write(json_buf, ", \"protocol\": %d", pkt_info->protocol);
    buffer_write(json_buf, ", \"from_port\": %d", pkt_info->src_port);
    buffer_write(json_buf, ", \"to_port\": %d", pkt_info->dest_port);
    buffer_write(json_buf, ", \"size\": %llu", pkt_info->payload_size);
    buffer_write(json_buf, ", \"count\": %llu }", pkt_info->packet_count);
    // temp fix; size is not actually tracked right now
    return 1;
    //return s;
}

unsigned int
dns_query_pkt_info2json(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt_info, buffer_t* json_buf) {
    unsigned int s = 0;
    node_t* src_node;
    // the 'node' that was queried; this could be a node that we already know
    node_t* dns_node;
    char dname_str[512];
    dns_dname2str(dname_str, dns_pkt_info->dname, 512);
    ip_t ip;

    ip.family = dns_pkt_info->family;
    memcpy(ip.addr, dns_pkt_info->ip, 16);

    spin_log(LOG_DEBUG, "[XX] creating dns query command\n");

    dns_node = node_cache_find_by_domain(node_cache, dname_str);
    if (dns_node == NULL) {
        // something went wrong, we should have just added t
        char pkt_str[1024];
        spin_log(LOG_ERR, "[XX] ERROR! DNS node not found in cache!\n");
        dns_pktinfo2str(pkt_str, dns_pkt_info, 1024);
        spin_log(LOG_DEBUG, "[XX] pktinfo: %s\n", pkt_str);
        spin_log(LOG_DEBUG, "[XX] node cache:\n");
        node_cache_print(node_cache);
        return 0;
    }

    src_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    if (src_node == NULL) {
        printf("[XX] error, src node not found in cache");
        char pkt_str[1024];
        spin_log(LOG_ERR, "[XX] ERROR! src node not found in cache!\n");
        dns_pktinfo2str(pkt_str, dns_pkt_info, 1024);
        spin_log(LOG_DEBUG, "[XX] pktinfo: %s\n", pkt_str);
        spin_log(LOG_DEBUG, "[XX] node cache:\n");
        node_cache_print(node_cache);
        return 0;
    }

    buffer_write(json_buf, "{ \"from\": ");
    s += node2json(src_node, json_buf);
    s += buffer_write(json_buf, ", \"queriednode\": ");
    s += node2json(dns_node, json_buf);
    s += buffer_write(json_buf, ", \"query\": \"%s\"", dname_str);
    s += buffer_write(json_buf, " }");

    spin_log(LOG_DEBUG, "[XX] dns query command created, size %d\n", s);
    // temp fix; size is not actually tracked right now
    return 1;
    //return s;
}

flow_list_t* flow_list_create(uint32_t timestamp) {

    flow_list_t* flow_list = (flow_list_t*)malloc(sizeof(flow_list_t));
    flow_list->flows = tree_create(cmp_pktinfos);
    flow_list->timestamp = timestamp;
    flow_list->total_size = 0;
    flow_list->total_count = 0;
    return flow_list;
}

void flow_list_destroy(flow_list_t* flow_list) {

    tree_destroy(flow_list->flows);
    free(flow_list);
}

void flow_list_add_pktinfo(flow_list_t* flow_list, pkt_info_t* pkt_info) {
    flow_data_t fd;
    flow_data_t* efd;
    tree_entry_t* cur = tree_find(flow_list->flows, 38, pkt_info);

    if (cur != NULL) {
        efd = (flow_data_t*)cur->data;
        efd->payload_size += pkt_info->payload_size;
        efd->packet_count += pkt_info->packet_count;
    } else {
        fd.payload_size = pkt_info->payload_size;
        fd.packet_count = pkt_info->packet_count;
        tree_add(flow_list->flows, 38, pkt_info, sizeof(fd), &fd, 1);
    }
}

int flow_list_should_send(flow_list_t* flow_list, uint32_t timestamp) {

    return timestamp > flow_list->timestamp;
}

void flow_list_clear(flow_list_t* flow_list, uint32_t timestamp) {

    tree_clear(flow_list->flows);
    flow_list->timestamp = timestamp;
}

int flow_list_empty(flow_list_t* flow_list) {

    return tree_empty(flow_list->flows);
}

unsigned int
flow_list2json(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf) {
    unsigned int s = 0;
    tree_entry_t* cur;
    pkt_info_t pkt_info;
    flow_data_t* fd;

    flow_list->total_size = 0;
    flow_list->total_count = 0;

    cur = tree_first(flow_list->flows);
    while (cur != NULL) {
        memcpy(&pkt_info, cur->key, 38);
        fd = (flow_data_t*) cur->data;
        pkt_info.payload_size = fd->payload_size;
        flow_list->total_size += fd->payload_size;
        pkt_info.packet_count = fd->packet_count;
        flow_list->total_count += fd->packet_count;
        s += pkt_info2json(node_cache, &pkt_info, json_buf);
        cur = tree_next(cur);
        if (cur != NULL) {
            buffer_write(json_buf, ", ");
        }
    }

    return s;
}

// note: this only does one pkt_info atm
unsigned int
create_traffic_command(node_cache_t* node_cache, flow_list_t* flow_list, buffer_t* json_buf, uint32_t timestamp) {
    unsigned int s = 0;

    buffer_write(json_buf, "{ \"command\": \"traffic\", \"argument\": \"\", ");
    buffer_write(json_buf, "\"result\": { \"flows\": [ ");
    s += flow_list2json(node_cache, flow_list, json_buf);
    buffer_write(json_buf, "], ");
    buffer_write(json_buf, " \"timestamp\": %u, ", timestamp);
    buffer_write(json_buf, " \"total_size\": %llu, ", flow_list->total_size);
    buffer_write(json_buf, " \"total_count\": %llu } }", flow_list->total_count);
    return s;
}
