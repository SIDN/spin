#include "spin_log.h"

#include <assert.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "util.h"
#include "spin_list.h"
#include "node_cache.h"
#include "netlink_commands.h"
#include "spin_cfg.h"
#include "spinhook.h"
#include "statistics.h"

STAT_MODULE(node_cache)

STAT_COUNTER(nodes, nodes, STAT_TOTAL);

#undef NEWMERGEDEBUG

node_t*
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
    node->modified = 0;
    node->persistent = 0;
    node->references = 0;
    node->device = NULL;
    return node;
}

void
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
    if (node->device) {
        free(node->device);
        node->device = NULL;
    }

    free(node);
}

void
cache_tree_add_keytonode(tree_t *totree, node_t* node, size_t key_len, void* key_data) {

    tree_add(totree, key_len, key_data, sizeof(node), (void *) &node , 1);
}

void
cache_tree_add_ip(node_cache_t *node_cache, node_t* node, ip_t* ip) {
    STAT_COUNTER(ctr, cache-tree-add-ip, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cache_tree_add_keytonode(node_cache->ip_refs, node, sizeof(ip_t),  ip);
}

void
cache_tree_remove_ip(node_cache_t *node_cache, ip_t* ip) {
    tree_entry_t* cur;
    STAT_COUNTER(ctr, cache-tree-remove-ip, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cur = tree_find(node_cache->ip_refs, sizeof(ip_t), ip);
    tree_remove_entry(node_cache->ip_refs, cur);
}

#ifdef NEWMERGEDEBUG
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

void node_tree_print_ip(tree_t *iptree) {
    tree_entry_t *cur;

    cur = tree_first(iptree);
    while (cur != NULL) {
        node_t *node;
        int nodeid;
        char ip_str[256];

        if (cur->data_size) {
            node = * ((node_t**) cur->data);
            nodeid = node->id;
        } else {
            nodeid = 0;
        }

        ip_key2str(ip_str, 256, cur->key);
        spin_log(LOG_ERR, "Node: %d has addr %s\n", nodeid, ip_str);

        cur = tree_next(cur);
    }
}

void node_tree_print_domain(tree_t *domaintree) {
    tree_entry_t *cur;

    cur = tree_first(domaintree);
    while (cur != NULL) {
        node_t *node;
        int nodeid;

        if (cur->data_size) {
            node = * ((node_t**) cur->data);
            nodeid = node->id;
        } else {
            nodeid = 0;
        }

        spin_log(LOG_ERR, "Node: %d has domain %s\n", nodeid, cur->key);

        cur = tree_next(cur);
    }
}

void cache_tree_print_ip(tree_t *iptree) {
    tree_entry_t *cur;

    cur = tree_first(iptree);
    while (cur != NULL) {
        node_t *node;
        char ip_str[256];
        tree_entry_t *innode;

        node = * ((node_t**) cur->data);

        ip_key2str(ip_str, 256, cur->key);
        spin_log(LOG_DEBUG, "Addr %s -> node %d\n", ip_str, node->id);

        innode = tree_find(node->ips, cur->key_size, cur->key);
        if (innode == 0) {
            spin_log(LOG_ERR, "Addr %s should have been in node %d\n", ip_str, node->id);
            node_tree_print_ip(node->ips);
        }

        cur = tree_next(cur);
    }
}

void cache_tree_print_domain(tree_t *iptree) {
    tree_entry_t *cur;

    cur = tree_first(iptree);
    while (cur != NULL) {
        node_t *node;
        tree_entry_t *innode;

        node = * ((node_t**) cur->data);

        spin_log(LOG_DEBUG, "Domain %s -> node %d\n", (char *) cur->key, node->id);

        innode = tree_find(node->domains, cur->key_size, cur->key);
        if (innode == 0) {
            spin_log(LOG_ERR, "Domain %s should have been in node %d\n", cur->key, node->id);
            node_tree_print_domain(node->domains);
        }

        cur = tree_next(cur);
    }
}
#endif

void cache_tree_print(node_cache_t *node_cache) {

#ifdef NEWMERGEDEBUG
    cache_tree_print_ip(node_cache->ip_refs);
    cache_tree_print_domain(node_cache->domain_refs);
#endif
}

void
node_add_ip(node_t* node, ip_t* ip) {
    STAT_COUNTER(ctr, add-ip, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    tree_add(node->ips, sizeof(ip_t), ip, 0, NULL, 1);
}

void
cache_tree_add_domain(node_cache_t *node_cache, node_t* node, char* domain) {
    STAT_COUNTER(ctr, cache-tree-add-domain, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cache_tree_add_keytonode(node_cache->domain_refs, node, strlen(domain) + 1, domain);
}

void
cache_tree_remove_domain(node_cache_t *node_cache, char* domain) {
    tree_entry_t* cur;
    STAT_COUNTER(ctr, cache-tree-remove-domain, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cur = tree_find(node_cache->domain_refs, strlen(domain) + 1, domain);
    tree_remove_entry(node_cache->domain_refs, cur);
}

void
node_add_domain(node_t* node, char* domain) {
    STAT_COUNTER(ctr, add-domain, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    tree_add(node->domains, strlen(domain) + 1, domain, 0, NULL, 1);
}

void
cache_tree_add_mac(node_cache_t *node_cache, node_t* node, char* mac) {
    STAT_COUNTER(ctr, cache-tree-add-mac, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cache_tree_add_keytonode(node_cache->mac_refs, node, strlen(mac) + 1, mac);
}

void
cache_tree_remove_mac(node_cache_t *node_cache, char* mac) {
    tree_entry_t* cur;
    STAT_COUNTER(ctr, cache-tree-remove-mac, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    cur = tree_find(node_cache->mac_refs, strlen(mac) + 1, mac);
    tree_remove_entry(node_cache->mac_refs, cur);
}

void
node_set_mac(node_t* node, char* mac) {
    STAT_COUNTER(ctr, set-mac, STAT_TOTAL);

    STAT_VALUE(ctr, mac != NULL);
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

void
node_set_modified(node_t* node, uint32_t last_seen) {
    node->modified = 1;
    node->last_seen = last_seen;
}

static void
node_merge(node_cache_t *node_cache, node_t* dest, node_t* src) {
    tree_entry_t* cur;
    int i;
    int m, modified = 0;
    STAT_COUNTER(ip_size, ip-tree-size, STAT_MAX);
    STAT_COUNTER(domain_size, domain-tree-size, STAT_MAX);
    STAT_COUNTER(modded, node-modified, STAT_TOTAL);

    if (dest->name == NULL) {
        node_set_name(dest, src->name);
        modified = 1;
    }
    if (dest->mac == NULL && src->mac != NULL) {
        node_set_mac(dest, src->mac);
        cache_tree_remove_mac(node_cache, src->mac);
        cache_tree_add_mac(node_cache, dest, src->mac);
        modified = 1;
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
        // Fix up global trees first
        ip_t *curip;

        curip = (ip_t *) cur->key;
        cache_tree_remove_ip(node_cache, curip);
        cache_tree_add_ip(node_cache, dest, curip);

        m = tree_add(dest->ips, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        if (m) {
            modified = 1;
        }
        cur = tree_next(cur);
    }
    STAT_VALUE(ip_size, tree_size(dest->ips));

    cur = tree_first(src->domains);
    while (cur != NULL) {
        // Fix up global trees first
        char *curdomain;

        curdomain = (char *) cur->key;
        cache_tree_remove_domain(node_cache, curdomain);
        cache_tree_add_domain(node_cache, dest, curdomain);

        m = tree_add(dest->domains, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        if (m) {
            modified = 1;
        }
        cur = tree_next(cur);
    }
    STAT_VALUE(domain_size, tree_size(dest->domains));

    STAT_VALUE(modded, modified);
    if (modified) {
        dest->modified = 1;
    }
}

#ifdef notdef
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

void node_callback_new(node_cache_t* node_cache, modfunc mf) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    node_t* node;
    int nfound;
    STAT_COUNTER(ctr, publish-new, STAT_TOTAL);

    nfound = 0;
    while (cur != NULL) {
        node = (node_t*)cur->data;
        if (node->modified) {
            (*mf)(node);
            nfound++;
            node->modified = 0;
        }
        cur = tree_next(cur);
    }
    STAT_VALUE(ctr, nfound);
}

void node_callback_devices(node_cache_t* node_cache, cleanfunc mf, void * ap) {
    tree_entry_t* cur;
    node_t* node;
    int nfound;
    STAT_COUNTER(ctr, publish-device, STAT_TOTAL);

    nfound = 0;
    cur = tree_first(node_cache->mac_refs);
    while (cur != NULL) {
        node = * ((node_t**) cur->data);
        assert(node->device);
        (*mf)(node_cache, node, ap);
        nfound++;
        cur = tree_next(cur);
    }
    STAT_VALUE(ctr, nfound);
}

/*
 * Create and destroy node_cache
 *
 */

node_cache_t*
node_cache_create() {
    node_cache_t* node_cache = (node_cache_t*)malloc(sizeof(node_cache_t));
    node_cache->nodes = tree_create(cmp_ints);

    node_cache->ip_refs = tree_create(cmp_ips);
    node_cache->domain_refs = tree_create(cmp_strs);
    node_cache->mac_refs = tree_create(cmp_strs);

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

void
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

node_t* node_cache_find_by_mac(node_cache_t* node_cache, char* macaddr) {
    node_t *node;
    tree_entry_t *leaf;
    STAT_COUNTER(ctr, find-by-mac, STAT_TOTAL);

    leaf = tree_find(node_cache->mac_refs, strlen(macaddr) + 1, macaddr);
    if (leaf != NULL) {
        node = * ((node_t**) leaf->data);
        STAT_VALUE(ctr, 1);
        return node;
    }

    STAT_VALUE(ctr, 0);
    return NULL;
}

node_t* node_cache_find_by_ip(node_cache_t* node_cache, size_t key_size, ip_t* ip) {
    node_t *node;
    tree_entry_t *leaf;
    STAT_COUNTER(ctr, find-by-ip, STAT_TOTAL);

    leaf = tree_find(node_cache->ip_refs, sizeof(ip_t), ip);
    if (leaf != NULL) {
        node = * ((node_t**) leaf->data);
        STAT_VALUE(ctr, 1);
        return node;
    }

    STAT_VALUE(ctr, 0);
    return NULL;
}

node_t* node_cache_find_by_domain(node_cache_t* node_cache, char* dname) {
    node_t *node;
    tree_entry_t *leaf;
    STAT_COUNTER(ctr, find-by-domain, STAT_TOTAL);

    leaf = tree_find(node_cache->domain_refs, strlen(dname) + 1, dname);
    if (leaf != NULL) {
        node = * ((node_t**) leaf->data);
        STAT_VALUE(ctr, 1);
        return node;
    }

    STAT_VALUE(ctr, 0);
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

static void
node_clean(node_cache_t *node_cache, node_t *node) {
    tree_entry_t *leaf;

    if (node->mac) {
        cache_tree_remove_mac(node_cache, node->mac);
    }
    leaf = tree_first(node->ips);
    while (leaf != NULL) {
        ip_t *curip;

        curip = (ip_t *) leaf->key;
        cache_tree_remove_ip(node_cache, curip);

        leaf = tree_next(leaf);
    }

    leaf = tree_first(node->domains);
    while (leaf != NULL) {
        char *curdomain;

        curdomain = (char *) leaf->key;
        cache_tree_remove_domain(node_cache, curdomain);

        leaf = tree_next(leaf);
    }
}

void node_cache_clean(node_cache_t* node_cache, uint32_t older_than) {
    tree_entry_t* cur = tree_first(node_cache->nodes);
    tree_entry_t* next;
    node_t* node;
    size_t deleted = 0;
    STAT_COUNTER(nretained, old-retained, STAT_TOTAL);

    spin_log(LOG_DEBUG, "[cache] clean up cache, timestamp %u\n", older_than);
    while (cur != NULL) {
        node = (node_t*)cur->data;
        next = tree_next(cur);
        if (node->last_seen < older_than) {
            if (!node->device && !node->references && !node->persistent) {
                spinhook_nodedeleted(node_cache, node);

                node_clean(node_cache, node);
                node_destroy(node);
                cur->data = NULL;
                tree_remove_entry(node_cache->nodes, cur);
                deleted++;
                STAT_VALUE(nretained, 1);
            } else {
                STAT_VALUE(nretained, 0);
            }
        }
        cur = next;
    }
    spin_log(LOG_DEBUG, "[node_cache] Removed %u entries older than %u, size now %u\n", deleted, older_than, tree_size(node_cache->nodes));

    cache_tree_print(node_cache);
}


static int
node_cache_get_new_id(node_cache_t* node_cache) {
    int nextid;
    STAT_COUNTER(nnodes, number-nodes, STAT_MAX);

    // just incremental for now
    nextid = node_cache->available_id++;
    STAT_VALUE(nnodes, node_cache->available_id);

    return nextid;
}

static void
add_mac_and_name(node_cache_t* node_cache, node_t* node, ip_t* ip) {
    char* mac;
    char* name;
    STAT_COUNTER(macctr, mac-found-by-read, STAT_TOTAL);

    mac = arp_table_find_by_ip(node_cache->arp_table, ip);
    if (!mac) {
        // todo: incorporate this in standard lookup?
        arp_table_read(node_cache->arp_table);
        mac = arp_table_find_by_ip(node_cache->arp_table, ip);
        STAT_VALUE(macctr, mac != NULL);
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

void
node_cache_update_arp(node_cache_t *node_cache, uint32_t timestamp) {
    tree_entry_t* leaf;
    node_t *node;
    char *mac;
    ip_t *ip;
    static uint32_t lasttime;

    // Code called when arp table seems out of date

    // Not too often
    if (timestamp - lasttime < 10) {
        return;
    }
    lasttime = timestamp;

    arp_table_read(node_cache->arp_table);

    // Make sure all mac addresses are in node table

    leaf = tree_first(node_cache->arp_table->entries);
    spin_log(LOG_DEBUG, "[arp table reread]\n");
    while (leaf != NULL) {
        mac = (char *) leaf->data;
        node = node_cache_find_by_mac(node_cache, mac);
        if (node == NULL) {
            ip = (ip_t *) leaf->key;
            // New MAC address
            node = node_create(0);
            node_set_modified(node, timestamp);
            add_mac_and_name(node_cache, node, ip);
            node_add_ip(node, ip);
            (void) node_cache_add_node(node_cache, node);
        }
        leaf = tree_next(leaf);
    }
}

static void
node_cache_add_ip_info(node_cache_t* node_cache, ip_t* ip, uint32_t timestamp) {
    // todo: add an search-by-ip tree and don't do anything if we
    // have this one already? (do set mac if now known,
    // and update last_seen)
    node_t* node;
    char* name;
    int new;
    STAT_COUNTER(ctr, add-ip-info, STAT_TOTAL);

    node = node_cache_find_by_ip(node_cache, sizeof(ip_t), ip);
    STAT_VALUE(ctr,  node == NULL);
    if (node != NULL) {
        node_set_last_seen(node, timestamp);
        return;
    }

    // It is new, go through the whole rigmarole

    node = node_create(0);
    node_set_modified(node, timestamp);
    add_mac_and_name(node_cache, node, ip);
    node_add_ip(node, ip);
    new = node_cache_add_node(node_cache, node);
    assert(new);

    // It was new; reread the DHCP leases table, and set the name if it wasn't set yet
    node_names_read_dhcpleases(node_cache->names, "/var/dhcp.leases");
    if (node->mac && !node->name) {
        name = node_names_find_mac(node_cache->names, node->mac);
        if (name != NULL) {
            node_set_name(node, name);
        }
    }
}

void node_cache_add_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info, uint32_t timestamp) {
    ip_t ip;
    STAT_COUNTER(ctr, add-pkt-info, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

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
    STAT_COUNTER(ctr, add-dns-info, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    ip.family = dns_pkt->family;
    memcpy(ip.addr, dns_pkt->ip, 16);
    dns_dname2str(dname_str, dns_pkt->dname, 512);

    node_t* node = node_create(0);
    node_set_modified(node, timestamp);
    node_add_ip(node, &ip);
    node_add_domain(node, dname_str);
    add_mac_and_name(node_cache, node, &ip);
    node_cache_add_node(node_cache, node);
}

void node_cache_add_dns_query_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt, uint32_t timestamp) {
    // first see if we have a node with this ip or domain already
    char dname_str[512];
    ip_t ip;
    STAT_COUNTER(ctr, add-dns-query, STAT_TOTAL);

    ip.family = dns_pkt->family;
    memcpy(ip.addr, dns_pkt->ip, 16);
    dns_dname2str(dname_str, dns_pkt->dname, 512);

    // add the node with the domain name; if it is not known
    // this will result in a 'node' with only the domain name
    node_t* node = node_create(0);
    node_set_modified(node, timestamp);
    node_add_domain(node, dname_str);
    node_cache_add_node(node_cache, node);

    // in this case, the dns_pkt's ip address is a separate node!
    // add it too if it does not exist
    node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
    STAT_VALUE(ctr, node == NULL);
    if (node == NULL) {
        node = node_create(0);
        node_set_modified(node, timestamp);
        node_add_ip(node, &ip);
        node_cache_add_node(node_cache, node);
    }
}

node_t *
oldnode(tree_t *reftree, size_t size, void *data) {
    node_t *node;
    tree_entry_t *oldleaf;

    oldleaf = tree_find(reftree, size, data);
   
    if (oldleaf != NULL) {
        assert(oldleaf->data_size == sizeof(node));

        node = * ((node_t**) oldleaf->data);
        return node;
    }
    
    return NULL;
}

#define MAXOLD  64

static void
add_node_to_ar(node_t *node, node_t **ar, int *nelem) {
    int i;

    /*
     * First check if already there
     */

    for(i=0; i< *nelem; i++) {
        if (ar[i] == node) {
            return;
        }
    }
    assert(*nelem < MAXOLD);
    ar[*nelem] = node;
    *nelem += 1;
}

static int
nodecompar(const void *a, const void *b) {
    node_t *na, *nb;

    na = *((node_t **) a);
    nb = *((node_t **) b);

    // If a device put in front
    if (na->device) {
        return -1;
    }
    if (nb->device) {
        return 1;
    }

    // If dummy 0 node put in back
    if (na->id == 0) {
        return 1;
    }
    if (nb->id == 0) {
        return -1;
    }

    // Lowest nodenumber first
    return (na->id - nb->id);
}

int
node_cache_add_node(node_cache_t *node_cache, node_t *node) {
    node_t *nodes_to_merge[MAXOLD];
    int i, nnodes_to_merge;
    tree_entry_t *leaf, *newleaf;
    node_t *existing_node, *src_node, *dest_node;
    int new_id, *new_id_mem;
    STAT_COUNTER(ctr, nodes-to-merge, STAT_MAX);

    assert(node->id == 0);

    /*
     * When merging the incoming node always participates
     */
    nodes_to_merge[0] = node;
    nnodes_to_merge = 1;

    /*
     * First find all nodes in the cache this node overlaps
     * We will merge all these nodes
     *
     * We look at Mac, Ip-addresses and Domains
     */

    if (node->mac) {
        existing_node = oldnode(node_cache->mac_refs, strlen(node->mac) + 1, node->mac);
        if (existing_node != NULL) {
            add_node_to_ar(existing_node, nodes_to_merge, &nnodes_to_merge);
        }
    }

    newleaf = tree_first(node->ips);

    while (newleaf != NULL) {
        existing_node = oldnode(node_cache->ip_refs, newleaf->key_size, newleaf->key);
        if (existing_node != NULL) {
            add_node_to_ar(existing_node, nodes_to_merge, &nnodes_to_merge);
        }
        
        newleaf = tree_next(newleaf);
    }

    newleaf = tree_first(node->domains);

    while (newleaf != NULL) {
        existing_node = oldnode(node_cache->domain_refs, newleaf->key_size, newleaf->key);
        if (existing_node != NULL) {
            add_node_to_ar(existing_node, nodes_to_merge, &nnodes_to_merge);
        }
        
        newleaf = tree_next(newleaf);
    }

    STAT_VALUE(ctr, nnodes_to_merge);

    cache_tree_print(node_cache);

    if (nnodes_to_merge > 1) {

        qsort(nodes_to_merge, nnodes_to_merge, sizeof(node), nodecompar);

        // Actually go merge
        dest_node = nodes_to_merge[0];
        for (i=1; i<nnodes_to_merge; i++) {
            int thisid;
            tree_entry_t *thisleaf;

            src_node = nodes_to_merge[i];

            if (src_node->device && dest_node->device) {
                spin_log(LOG_ERR, "Merge two devices!!!\n");
            }

            thisid = src_node->id;

            // spin_log(LOG_DEBUG, "Go and merge node %d into %d\n", thisid, dest_node->id);
            node_merge(node_cache, dest_node, src_node);
            if (thisid != 0) {
                spinhook_nodesmerged(node_cache, dest_node, src_node);
            }
            node_destroy(src_node);
            if (thisid != 0) {
                // Existing nodes must be taken out of tree
                thisleaf = tree_find(node_cache->nodes, sizeof(thisid), &thisid);
                // set data to 0, else it will be freed again
                thisleaf->data = NULL;
                tree_remove_entry(node_cache->nodes, thisleaf);
            }
        }
        if (dest_node->mac && dest_node->device==NULL) {
            // Remaining node must be promoted to device

            spinhook_makedevice(node);
        }
        return 0;
    }

    // ok no shared elements at all, add as a new node
    new_id = node_cache_get_new_id(node_cache);
    new_id_mem = (int*) malloc(sizeof(new_id));
    memcpy(new_id_mem, &new_id, sizeof(new_id));
    node->id = new_id;

    tree_add(node_cache->nodes, sizeof(new_id), new_id_mem, sizeof(node_t), node, 0);

    /*
     * Add cache tree entries for previous node 0
     */

    spin_log(LOG_DEBUG, "Just created node %d\n", node->id);

    if (node->mac) {
        cache_tree_add_mac(node_cache, node, node->mac);
        spinhook_makedevice(node);
    }

    leaf = tree_first(node->ips);
    while (leaf != NULL) {
        cache_tree_add_ip(node_cache, node, (ip_t*) leaf->key);
        leaf = tree_next(leaf);
    }

    leaf = tree_first(node->domains);
    while (leaf != NULL) {
        cache_tree_add_domain(node_cache, node, (char *) leaf->key);
        leaf = tree_next(leaf);
    }

    return 1;
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
    STAT_COUNTER(ctr, flow-exist, STAT_TOTAL);

    STAT_VALUE(ctr, cur != NULL);
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
