
/*
 * Functionality for a DOTS signal home prototype
 *
 * These functions match DOTS messages (or a subset thereof)
 * to flows.
 *
 * There are in theory 3 ways we can do the matching; an ideal solution
 * might use a combination of these
 *
 * 1. Keep a history of all flows, and go through them to match any DOTS request
 * 2. Match DOTS requests to the small subset of 'last flows' we already keep for devices
 * 3. Store the DOTS reports, and match any new flow against them
 *
 * For now, we'll implement 2; the basic functionality we need here is similar
 * (ie. match device+flow to message)
 */

#include "dots.h"
#include "spin_log.h"

#define MAX_ERR_LEN 256
static char error_message[MAX_ERR_LEN];

// Get the child object with the given name
// return 0 on success, >0 on error, at which point error_message will be set
int get_object(spin_data parent, spin_data* result, const char* name, int type) {
    *result = cJSON_GetObjectItemCaseSensitive(parent, name);
    if (*result == NULL) {
        snprintf(error_message, MAX_ERR_LEN, "Missing field %s in %s", name, parent->string);
        return 1;
    }
    if ((*result)->type != type) {
        snprintf(error_message, MAX_ERR_LEN, "Wrong type of field %s in %s, found %d, expected %d", name, parent->string, (*result)->type, type);
        return 2;
    }

    return 0;
}


int process_dots_signal(node_cache_t* node_cache, spin_data dots_message, char** error) {
    char* str;
    spin_log(LOG_DEBUG, "Processing DOTS signal\n");

    str = cJSON_Print(dots_message);
    spin_log(LOG_DEBUG, "Message: %s\n", str);
    free(str);
    spin_data mitigation_request;
    if (get_object(dots_message, &mitigation_request, "ietf-dots-signal-channel:mitigation-scope", cJSON_Object) != 0) {
        *error = error_message;
        return 1;
    }
    spin_data scope;
    if (get_object(mitigation_request, &scope, "scope", cJSON_Array) != 0) {
        *error = error_message;
        return 1;
    }
    // for each element in scope, check if a device matches
    for (int i = 0 ; i < cJSON_GetArraySize(scope) ; i++) {
        cJSON* scope_item = cJSON_GetArrayItem(scope, i);
        spin_data target_prefix;
        spin_data source_prefix;
        spin_data lifetime;
        if (get_object(scope_item, &target_prefix, "target-prefix", cJSON_Array) != 0) {
            *error = error_message;
            return 1;
        }
        spin_log(LOG_DEBUG, "Target prefix check not implemented yet\n");
        if (get_object(scope_item, &source_prefix, "ietf-dots-call-home:source-prefix", cJSON_Array) != 0) {
            *error = error_message;
            return 1;
        }
        if (get_object(scope_item, &lifetime, "lifetime", cJSON_Number) != 0) {
            *error = error_message;
            return 1;
        }
        // TODO: What to do with lifetime? (if anything)
        for (int j = 0; j < cJSON_GetArraySize(target_prefix); j++) {
            // Before we refactor this into something else,
            // perhaps we should consider a different approach:
            // first see in the cache whether we have any
            // node that falls under the prefixes,
            // and then go through the flows of all devices;
            // not the other way around.
            //spin_data c_prefix = cJSON_GetArrayItem(target_prefix, j);
            const char* prefix_str = cJSON_GetArrayItem(target_prefix, j)->valuestring;
            spin_log(LOG_DEBUG, "Checking prefix: %s\n", prefix_str);
            ip_t prefix_ip;
            if (spin_pton(&prefix_ip, prefix_str)) {
                tree_entry_t* cur = tree_first(node_cache->mac_refs);
                while (cur != NULL) {
                    //node_t* node = (node_t*)cur->data;
                    node_t* node = * ((node_t**) cur->data);

                    if (node->device) {
                        printf("[XX] CHECKING DEVICE\n");
                        printf("[XX] node at %p\n", node);
                        printf("[XX] device at %p\n", node->device);
                        printf("[XX] mac is at %s\n", node->mac);
                        printf("[XX] dv_flowtree at %p\n", node->device->dv_flowtree);
                        
                        tree_entry_t* flow_entry = tree_first(node->device->dv_flowtree);
                        while (flow_entry != NULL) {
                            printf("[XX] flow entry at %p\n", flow_entry);
                            // id is the id of the node this device had contact with
                            // check its addresses for a potential match
                            int* node_id = (int*)flow_entry->key;
                            printf("[XX] dest node id %u\n", *node_id);
                            node_t* dest_node = node_cache_find_by_id(node_cache, *node_id);
                            tree_entry_t* ip_entry = tree_first(dest_node->ips);
                            while (ip_entry != NULL) {
                                printf("[XX] Checking dest node IP\n");
                                ip_t* dest_ip = (ip_t*)ip_entry->key;
                                char dest_ip_str[140];
                                spin_ntop(dest_ip_str, dest_ip, 140);
                                printf("[XX] Checking dest node ip address: %s\n", dest_ip_str);
                                if (ip_in_net(dest_ip, &prefix_ip)) {
                                    printf("[XX] WE HAVE A MATCH\n");
                                }
                                ip_entry = tree_next(ip_entry);
                            }
                            flow_entry = tree_next(flow_entry);
                        }
                    }
                    cur = tree_next(cur);
                }
                //ip_in_net(&ip_a, &prefix_ip);
            }
            // Loop over all devices' recent history entries
        }
    }

    *error = "Not implemented yet";
    return 1;
}
