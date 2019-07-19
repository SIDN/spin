#include <assert.h>

#include "tree.h"
#include "node_cache.h"
#include "spin_log.h"

#include "spinhook.h"
#include "spindata.h"

#include "statistics.h"

STAT_MODULE(spindata)

char *
spin_data_serialize(spin_data sd) {
    char *result;

    result = cJSON_PrintUnformatted(sd);

    // result is malloced, should be freed
    return result;
}

void
spin_data_ser_delete(char *str) {

    free(str);
}

void
spin_data_delete(spin_data sd) {

    cJSON_Delete(sd);
}

spin_data
spin_data_nodes_merged(int node1, int node2) {
    cJSON *resobj;

    resobj = cJSON_CreateObject();
    cJSON_AddNumberToObject(resobj, "id", node1);
    cJSON_AddNumberToObject(resobj, "merged-to", node2);

    return resobj;
}

spin_data
spin_data_node_deleted(int node) {
    cJSON *resobj;

    resobj = cJSON_CreateObject();
    cJSON_AddNumberToObject(resobj, "id", node);

    return resobj;
}

//  Also needed for lists
spin_data
spin_data_ipar(tree_t *iptree) {
    cJSON *arobj, *strobj;
    tree_entry_t* cur;
    char ip_str[INET6_ADDRSTRLEN];
    STAT_COUNTER(ctr, spindata-ipar, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    arobj = cJSON_CreateArray();
    cur = tree_first(iptree);
    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        strobj = cJSON_CreateString(ip_str);
        cJSON_AddItemToArray(arobj, strobj);
        cur = tree_next(cur);
    }
    return arobj;
}

spin_data
spin_data_node(node_t* node) {
    cJSON *nodeobj;
    cJSON *arobj;
    cJSON *strobj;
    tree_entry_t* cur;
    STAT_COUNTER(ctr, spindata-node, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    nodeobj = cJSON_CreateObject();
    cJSON_AddNumberToObject(nodeobj, "id", node->id);
    if (node->name != NULL) {
        cJSON_AddStringToObject(nodeobj, "name", node->name);
    }
    if (node->mac != NULL) {
        cJSON_AddStringToObject(nodeobj, "mac", node->mac);
    }
    if (node->is_blocked) {
        cJSON_AddBoolToObject(nodeobj, "is_blocked", 1);
    }
    if (node->is_allowed) {
        cJSON_AddBoolToObject(nodeobj, "is_excepted", 1);
    }
    cJSON_AddNumberToObject(nodeobj, "lastseen", node->last_seen);

    arobj = spin_data_ipar(node->ips);
    cJSON_AddItemToObject(nodeobj, "ips", arobj);

    // domains
    arobj = cJSON_CreateArray();
    cur = tree_first(node->domains);
    while (cur != NULL) {
        strobj = cJSON_CreateString((char*)cur->key);
        cJSON_AddItemToArray(arobj, strobj);
        cur = tree_next(cur);
    }
    cJSON_AddItemToObject(nodeobj, "domains", arobj);

    return nodeobj;
}

static spin_data
spin_data_noderef(node_t *node) {
    cJSON* nodeobj;
    STAT_COUNTER(ctr, spindata-noderef, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    nodeobj = cJSON_CreateNumber(node->id);
    return nodeobj;
}

static void
device_node(node_cache_t *node_cache, node_t *node, void *ap) {
    cJSON *arobj = (cJSON *) ap;
    spin_data nodeobj;

    nodeobj = spin_data_node(node);
    cJSON_AddItemToArray(arobj, nodeobj);
}

spin_data
spin_data_devicelist(node_cache_t *node_cache) {
    cJSON *node_ar_obj;

    node_ar_obj = cJSON_CreateArray();
    node_callback_devices(node_cache, device_node, (void *) node_ar_obj);

    return node_ar_obj;
}

spin_data
spin_data_flowlist(node_t *node) {
    cJSON *flow_ar_obj, *flow_obj;
    int *nodenump;
    devflow_t *dfp;
    tree_entry_t* cur;

    flow_ar_obj = cJSON_CreateArray();
    if (node->device) {
        cur = tree_first(node->device->dv_flowtree);
        while (cur != NULL) {
            nodenump = (int *) cur->key;
            dfp = (devflow_t *) cur->data;
            flow_obj = cJSON_CreateObject();
            if (dfp->dvf_blocked) {
                cJSON_AddNumberToObject(flow_obj, "blocked", 1);
            } else {
                cJSON_AddNumberToObject(flow_obj, "to", *nodenump);
                cJSON_AddNumberToObject(flow_obj, "packets", dfp->dvf_packets);
                cJSON_AddNumberToObject(flow_obj, "bytes", dfp->dvf_bytes);
                cJSON_AddNumberToObject(flow_obj, "lastseen", dfp->dvf_lastseen);
            }

            cJSON_AddItemToArray(flow_ar_obj, flow_obj);

            cur = tree_next(cur);
        }
    }
    return flow_ar_obj;
}

static node_t *lookup_ip(node_cache_t *node_cache, ip_t *ip, pkt_info_t *pkt_info, char *sd) {
    node_t *result;

    result = node_cache_find_by_ip(node_cache, sizeof(ip_t), ip);
    if (result == NULL) {
        char pkt_str[1024];
        spin_log(LOG_ERR, "[XX] ERROR! %s node not found in cache!\n");
        pktinfo2str(pkt_str, pkt_info, 1024);
        spin_log(LOG_DEBUG, "[XX] pktinfo: %s\n", pkt_str);
        spin_log(LOG_DEBUG, "[XX] node cache:\n");
        node_cache_print(node_cache);
        return 0;
    }
    return result;
}

spin_data
spin_data_pkt_info(node_cache_t* node_cache, pkt_info_t* pkt_info) {
    cJSON *pktobj;
    node_t* src_node;
    node_t* dest_node;
    ip_t ip;
    STAT_COUNTER(ctr, spindata-pktinfo, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    ip.family = pkt_info->family;
    memcpy(ip.addr, pkt_info->src_addr, 16);
    src_node = lookup_ip(node_cache, &ip, pkt_info, "src");
    memcpy(ip.addr, pkt_info->dest_addr, 16);
    dest_node = lookup_ip(node_cache, &ip, pkt_info, "dst");
    if (src_node == NULL || dest_node == NULL) {
        return 0;
    }

    pktobj = cJSON_CreateObject();

    cJSON_AddItemToObject(pktobj, "from", spin_data_noderef(src_node));
    cJSON_AddItemToObject(pktobj, "to", spin_data_noderef(dest_node));

    cJSON_AddNumberToObject(pktobj, "protocol", pkt_info->protocol);
    cJSON_AddNumberToObject(pktobj, "from_port", pkt_info->src_port);
    cJSON_AddNumberToObject(pktobj, "to_port", pkt_info->dest_port);
    cJSON_AddNumberToObject(pktobj, "size", pkt_info->payload_size);
    cJSON_AddNumberToObject(pktobj, "count", pkt_info->packet_count);

    // spinhook_traffic(src_node, dest_node, pkt_info->packet_count, pkt_info->payload_size);

    return(pktobj);
}

#define DNAME_SIZE  512
spin_data
spin_data_dns_query_pkt_info(node_cache_t* node_cache, dns_pkt_info_t* dns_pkt_info) {
    cJSON *pktobj;
    node_t* src_node;
    // the 'node' that was queried; this could be a node that we already know
    node_t* dns_node;
    char dname_str[DNAME_SIZE];
    ip_t ip;
    STAT_COUNTER(ctr, spindata-dnsquery, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    dns_dname2str(dname_str, dns_pkt_info->dname, DNAME_SIZE);

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

    pktobj = cJSON_CreateObject();

    cJSON_AddItemToObject(pktobj, "from", spin_data_noderef(src_node));
    cJSON_AddItemToObject(pktobj, "queriednode", spin_data_noderef(dns_node));
    cJSON_AddStringToObject(pktobj, "query", dname_str);

    return pktobj;
}

static spin_data
flow_list2json(node_cache_t* node_cache, flow_list_t* flow_list) {
    cJSON *flobj, *pktobj;
    tree_entry_t* cur;
    pkt_info_t pkt_info;
    flow_data_t* fd;
    STAT_COUNTER(ctr, spindata-flowlist, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    flobj = cJSON_CreateArray();

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

        pktobj = spin_data_pkt_info(node_cache, &pkt_info);
        cJSON_AddItemToArray(flobj, pktobj);

        cur = tree_next(cur);
    }

    return flobj;
}


spin_data
spin_data_create_mqtt_command(const char* command, char* argument, spin_data result) {
    cJSON *cmdobj;
    STAT_COUNTER(ctr, spindata-mqttcommand, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    cmdobj = cJSON_CreateObject();
    if (command != NULL) {
        cJSON_AddStringToObject(cmdobj, "command", command);
    }
    if (argument != NULL) {
        cJSON_AddStringToObject(cmdobj, "argument", argument);
    }
    if (result != NULL) {
        cJSON_AddItemToObject(cmdobj, "result", result);
    }

    return cmdobj;
}

spin_data
spin_data_create_traffic(node_cache_t* node_cache, flow_list_t* flow_list, uint32_t timestamp) {
    cJSON *trobj;
    cJSON *flobj;
    STAT_COUNTER(ctr, spindata-traffic, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    flobj = flow_list2json(node_cache, flow_list);

    trobj = cJSON_CreateObject();
    cJSON_AddItemToObject(trobj, "flows", flobj);
    cJSON_AddNumberToObject(trobj, "timestamp", timestamp);
    cJSON_AddNumberToObject(trobj, "total_size", flow_list->total_size);
    cJSON_AddNumberToObject(trobj, "total_count", flow_list->total_count);

    return spin_data_create_mqtt_command("traffic", "", trobj);
}

spin_data
spin_data_nodepairtree(tree_t* tree) {
    cJSON *arobj, *npobj;
    tree_entry_t* cur;
    int *nodenump;
    STAT_COUNTER(ctr, spindata-nodepairtree, STAT_TOTAL);

    STAT_VALUE(ctr, 1);

    arobj = cJSON_CreateArray();

    cur = tree_first(tree);
    while (cur != NULL) {
        nodenump = (int *) cur->key;

        npobj = cJSON_CreateObject();
        cJSON_AddNumberToObject(npobj, "node1", nodenump[0]);
        cJSON_AddNumberToObject(npobj, "node2", nodenump[1]);
        cJSON_AddItemToArray(arobj, npobj);

        cur = tree_next(cur);
    }

    return arobj;
}
