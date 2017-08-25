
#include "node_cache.h"

#include <assert.h>
#include <arpa/inet.h>
#include "util.h"

node_t*
node_create(int id) {
    node_t* node = (node_t*) malloc(sizeof(node_t));
    node->id = id;
    node->ips = tree_create(cmp_ips);
    node->domains = tree_create(cmp_domains);
    node->name = NULL;
    node->mac = NULL;
    node->last_seen = 0;
    return node;
}

void
node_destroy(node_t* node) {
    tree_destroy(node->ips);
    node->ips = NULL;
    tree_destroy(node->domains);
    node->domains = NULL;
    if (node->mac) {
        free(node->mac);
    }
    if (node->name) {
        free(node->name);
    }
    free(node);
}

node_t* node_clone(node_t* node) {
    node_t* new = node_create(node->id);
    tree_entry_t *cur;
    if (node->mac) {
        node_set_mac(new, node->mac);
    }
    if (node->name) {
        node_set_name(new, node->name);
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

void
node_add_ip(node_t* node, ip_t* ip) {
    tree_add(node->ips, sizeof(ip_t), ip, 0, NULL, 1);
}

void
node_add_domain(node_t* node, char* domain) {
    tree_add(node->domains, strlen(domain) + 1, domain, 0, NULL, 1);
}

void
node_set_mac(node_t* node, char* mac) {
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
    if (name == NULL) {
        return;
    }
    if (node->name != NULL) {
        free(node->name);
    }
    node->name = strndup(name, 128);
}

void
node_set_last_seen(node_t* node, uint32_t last_seen) {
    node->last_seen = last_seen;
}

int
node_shares_element(node_t* node, node_t* othernode) {
    tree_entry_t* cur_me;

    //printf("[XX] ips  at %p\n", node->ips);
    //fflush(stdout);
    if (node->mac != NULL && othernode->mac != NULL) {
        if (strcmp(node->mac, othernode->mac) == 0) {
            return 1;
        }
    }

    cur_me = tree_first(node->ips);
    while (cur_me != NULL) {
        if (tree_find(othernode->ips, cur_me->key_size, cur_me->key) != NULL) {
            return 1;
        }
        cur_me = tree_next(cur_me);
    }

    cur_me = tree_first(node->domains);
    while (cur_me != NULL) {
        if (tree_find(othernode->domains, cur_me->key_size, cur_me->key) != NULL) {
            return 1;
        }
        cur_me = tree_next(cur_me);
    }

    return 0;
}

void
node_merge(node_t* dest, node_t* src) {
    tree_entry_t* cur;

    if (dest->name == NULL) {
        node_set_name(dest, src->name);
    }
    if (dest->mac == NULL) {
        node_set_mac(dest, src->mac);
    }
    if (dest->last_seen < src->last_seen) {
        dest->last_seen = src->last_seen;
    }
    cur = tree_first(src->ips);
    while (cur != NULL) {
        tree_add(dest->ips, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    cur = tree_first(src->domains);
    while (cur != NULL) {
        tree_add(dest->domains, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
}

void node_print(node_t* node) {
    tree_entry_t* cur;
    int fam;
    unsigned char* keyp;
    char str[512];

    printf("[NODE] id: %d\n", node->id);
    if (node->name != NULL) {
        printf("       name: %s\n", node->name);
    }
    if (node->mac != NULL) {
        printf("      mac: %s\n", node->mac);
    }
    printf("      ips:\n");
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
        printf("        %s\n", str);
        cur = tree_next(cur);
    }
    printf("      domains:\n");
    cur = tree_first(node->domains);
    while (cur != NULL) {
        printf("        %s\n", (unsigned char*)cur->key);
        cur = tree_next(cur);
    }
}

unsigned int
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


node_cache_t*
node_cache_create() {
    node_cache_t* node_cache = (node_cache_t*)malloc(sizeof(node_cache_t));
    node_cache->nodes = tree_create(cmp_ints);

    node_cache->ip_refs = tree_create(cmp_ips);

    node_cache->available_id = 1;

    node_cache->arp_table = arp_table_create();
    node_cache->names = node_names_create();
    node_names_read_dhcpconfig(node_cache->names, "/etc/config/dhcp");
    //node_names_read_userconfig(node_cache->names, "/etc/spin/spin_userdata.cfg");
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

void node_cache_print(node_cache_t* node_cache) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* cur_node;
    printf("[node cache]\n");
    while (cur != NULL) {
        cur_node = (node_t*) cur->data;
        //node_print(cur_node);
        node_print(cur_node);
        cur = tree_next(cur);
    }
    printf("[end of node cache]\n");

}

node_t* node_cache_find_by_ip(node_cache_t* node_cache, size_t key_size, ip_t* ip) {
    // TODO: this is very inefficient; we should add a second tree for ip searching
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* node;
    while (cur != NULL) {
        node = (node_t*)cur->data;
        // can we use a node_has_ip?
        if (tree_find(node->ips, sizeof(ip_t), ip) != NULL) {
            return node;
        }
        cur = tree_next(cur);
    }
    return NULL;
}

node_t* node_cache_find_by_domain(node_cache_t* node_cache, char* dname) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* node;
    while (cur != NULL) {
        node = (node_t*)cur->data;
        // can we use a node_has_domain?
        if (tree_find(node->domains, strlen(dname) + 1, dname) != NULL) {
            return node;
        }
        cur = tree_next(cur);
    }
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

int node_cache_get_new_id(node_cache_t* node_cache) {
    // just incremental for now
    return node_cache->available_id++;
}

void
add_mac_and_name(node_cache_t* node_cache, node_t* node, ip_t* ip) {
    char* mac = arp_table_find_by_ip(node_cache->arp_table, ip);
    char* name;
    if (!mac) {
        // todo: incorporate this in standard lookup?
        arp_table_read(node_cache->arp_table);
        mac = arp_table_find_by_ip(node_cache->arp_table, ip);
    }
    if (mac) {
        node_set_mac(node, mac);
        name = node_names_find_mac(node_cache->names, mac);
        if (name != NULL) {
            node_set_name(node, name);
        }
    } else {
        name = node_names_find_ip(node_cache->names, ip);
        if (name != NULL) {
            node_set_name(node, name);
        }
    }
}

void
node_cache_add_ip_info(node_cache_t* node_cache, ip_t* ip, uint32_t timestamp) {
    // todo: add an search-by-ip tree and don't do anything if we
    // have this one already? (do set mac if now known,
    // and update last_seen)
    node_t* node = node_create(0);
    node_set_last_seen(node, timestamp);
    add_mac_and_name(node_cache, node, ip);
    node_add_ip(node, ip);
    node_cache_add_node(node_cache, node);
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
    // first see if we have a node with this ip or domain already
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

void
node_cache_add_node(node_cache_t* node_cache, node_t* node) {
    int new_id, *new_id_mem;
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* tree_node;
    int node_found = 0;
    tree_entry_t* nxt;

    //printf("[XX] ADDING NODE old cache:\n");
    //node_cache_print(node_cache);

    while (cur != NULL) {
        tree_node = (node_t*) cur->data;
        if (node_shares_element(node, tree_node)) {
            if (!node_found) {
                node_merge(tree_node, node);
                node_found = 1;
                // TODO: walk the rest of the cache two, we may need to merge more nodes now
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
    if (node_found) {
        return;
    }
    // ok no elements at all, add as a new one
    new_id = node_cache_get_new_id(node_cache);
    new_id_mem = (int*) malloc(sizeof(new_id));
    memcpy(new_id_mem, &new_id, sizeof(new_id));
    node->id = new_id;
    tree_add(node_cache->nodes, sizeof(new_id), new_id_mem, sizeof(node_t), node, 0);

    //printf("[XX] DONE ADDING NODE new cache:\n");
    //node_cache_print(node_cache);
}

/*
{
    "to": {"id":1,"lastseen":1502702327,"ips":["::1"],"domains":[]},
    "from": {"id":1,"lastseen":1502702327,"ips":["::1"],"domains":[]},
    "to_port":1883,
    "size":744,
    "count":2,
    "from_port":56082
}
*/
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
        printf("[XX] ERROR! src node not found in cache!\n");
        pktinfo2str(pkt_str, pkt_info, 1024);
        printf("[XX] pktinfo: %s\n", pkt_str);
        printf("[XX] node cache:\n");
        node_cache_print(node_cache);
    }
    if (src_node == NULL) {
        char pkt_str[1024];
        printf("[XX] ERROR! dest node not found in cache!\n");
        pktinfo2str(pkt_str, pkt_info, 1024);
        printf("[XX] pktinfo: %s\n", pkt_str);
        printf("[XX] node cache:\n");
        node_cache_print(node_cache);
    }
    assert(src_node != NULL);
    assert(dest_node != NULL);

    buffer_write(json_buf, "{ \"from\": ");
    s += node2json(src_node, json_buf);
    buffer_write(json_buf, ", \"to\": ");
    s += node2json(dest_node, json_buf);
    buffer_write(json_buf, ", \"from_port\": %d", pkt_info->src_port);
    buffer_write(json_buf, ", \"to_port\": %d", pkt_info->dest_port);
    buffer_write(json_buf, ", \"size\": %d", pkt_info->payload_size);
    buffer_write(json_buf, ", \"count\": %d }", pkt_info->packet_count);
    return s;
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
    buffer_write(json_buf, " \"total_size\": %u, ", flow_list->total_size);
    buffer_write(json_buf, " \"total_count\": %u } }", flow_list->total_count);
    return s;
}
