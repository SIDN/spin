
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
#include "spin_config.h"
#include "rpc_json.h"

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

// returns 1 if the flow matches the given mitigation request scope
/*
int check_dots_match(spin_data scope, flow_entry_t* flow) {

}
*/

/*
 * Returns 0 if the signal was processed correctly (whether or not
 * there were any matches), 1 if there was an error. In the error case,
 * a static error string will be places at **error
 *
 * *matches is set to 1 if one of the flow matches the dots message
 *                    0 if not
 */
int dots_match_flows(node_cache_t* node_cache, spin_data scope, tree_entry_t* flow_entry, int* matches, char** error) {
    devflow_key_t* key = (devflow_key_t*) flow_entry->key;
    int node_id = key->dst_node_id;
    int dest_port = key->dst_port;
    int icmp_type = key->icmp_type;
    int i,j;
    node_t* dest_node = node_cache_find_by_id(node_cache, node_id);

    *matches = 0;

    if (!dest_node) {
        return 0;
    }

    for (i = 0 ; i < cJSON_GetArraySize(scope) ; i++) {
        cJSON* scope_item = cJSON_GetArrayItem(scope, i);
        spin_data target_prefix;

        // match if any ip matches any prefix
        if (get_object(scope_item, &target_prefix, "target-prefix", cJSON_Array) != 0) {
            // MUST have at least one target prefix
            *error = error_message;
            return 1;
        }
        for (j = 0; j < cJSON_GetArraySize(target_prefix); j++) {
            const char* prefix_str = cJSON_GetArrayItem(target_prefix, j)->valuestring;
            spin_log(LOG_DEBUG, "Checking prefix: %s\n", prefix_str);
            ip_t prefix_ip;
            if (prefix_str == NULL) {
                spin_log(LOG_ERR, "[dots] Unable to read prefix in DOTS message\n");
                *error = "Malformed DOTS message, unable to read prefix";
                return 1;
            }
            if (spin_pton(&prefix_ip, prefix_str)) {
                tree_entry_t* ip_entry = tree_first(dest_node->ips);
                while (ip_entry != NULL) {
                    ip_t* dest_ip = (ip_t*)ip_entry->key;
                    char dest_ip_str[140];
                    spin_ntop(dest_ip_str, dest_ip, 140);
                    spin_log(LOG_DEBUG, "[dots] Checking dest node ip address: %s\n", dest_ip_str);

                    if (ip_in_net(dest_ip, &prefix_ip)) {
                        *matches = 1;
                    }
                    ip_entry = tree_next(ip_entry);
                }
            }
        }
        // Already no match, move to next scope
        if (!*matches) {
            continue;
        }

        // If a port range is specified, it must match that as well
        spin_data target_port_range_list;
        if (get_object(scope_item, &target_port_range_list, "target-port-range", cJSON_Array) == 0) {
            // reset the match
            *matches = 0;
            for (i=0; i < cJSON_GetArraySize(target_port_range_list); i++) {
                cJSON* target_port_range = cJSON_GetArrayItem(target_port_range_list, i);
                // if only 'upper' is given, we want an exact match.
                // if 'upper' and 'lower' are given, it should be in-between
                spin_data upper_port_data;
                spin_data lower_port_data;
                if (get_object(target_port_range, &lower_port_data, "lower-port", cJSON_Number) != 0) {
                    *error = "Missing 'lower-port' in target-port-range";
                    return 1;
                }
                if (get_object(target_port_range, &upper_port_data, "upper-port", cJSON_Number) == 0) {
                    if (dest_port <= upper_port_data->valueint && dest_port >= lower_port_data->valueint) {
                        *matches = 1;
                    }
                } else {
                    if (dest_port == lower_port_data->valueint) {
                        *matches = 1;
                    }
                }
            }
        }

        // Already no match, move to next scope
        if (!*matches) {
            continue;
        }
        // If a port range is specified, it must match that as well
        spin_data target_icmp_type_range_list;
        if (get_object(scope_item, &target_icmp_type_range_list, "source-icmp-type-range", cJSON_Array) == 0) {
            // reset the match
            *matches = 0;
            for (i=0; i < cJSON_GetArraySize(target_icmp_type_range_list); i++) {
                cJSON* target_icmp_type_range = cJSON_GetArrayItem(target_icmp_type_range_list, i);
                // if only 'upper' is given, we want an exact match.
                // if 'upper' and 'lower' are given, it should be in-between
                spin_data upper_icmp_type_data;
                spin_data lower_icmp_type_data;
                if (get_object(target_icmp_type_range, &lower_icmp_type_data, "lower-type", cJSON_Number) != 0) {
                    *error = "Missing 'lower-type' in target-icmp-type-range";
                    return 1;
                }
                if (get_object(target_icmp_type_range, &upper_icmp_type_data, "upper-type", cJSON_Number) == 0) {
                    if (icmp_type <= upper_icmp_type_data->valueint && icmp_type >= lower_icmp_type_data->valueint) {
                        *matches = 1;
                    }
                } else {
                    if (icmp_type == lower_icmp_type_data->valueint) {
                        *matches = 1;
                    }
                }
            }
        }

    }

    return 0;
}

/*
 * Returns 0 if the signal was processed correctly (whether or not
 * there were any matches), 1 if there was an error. In the error case,
 * a static error string will be places at **error
 */
int
process_dots_signal(node_cache_t* node_cache, spin_data dots_message, char** error) {
    char* str;
    int devices_matching = 0;

    spin_log(LOG_DEBUG, "Processing DOTS signal\n");

    if (!spinconfig_dots_enabled()) {
        *error = "DOTS support is not enabled in spind.conf";
        return 1;
    }

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

    // Loop over all devices (i.e. all nodes with a mac), and check
    // their flow history
    tree_entry_t* cur = tree_first(node_cache->mac_refs);
    while (cur != NULL) {
        node_t* node = * ((node_t**) cur->data);
        if(!node->device) {
            *error = "Internal error, node with mac not marked as device";
            return 1;
        }

        tree_entry_t* flow_entry = tree_first(node->device->dv_flowtree);
        while (flow_entry != NULL) {
            int matches = 0;
            int result = dots_match_flows(node_cache, scope, flow_entry, &matches, error);
            if (result != 0) {
                // error was set by dots_match_flows
                return result;
            }
            if (matches) {
                devices_matching++;
                spin_log(LOG_INFO, "[dots] Found matching traffic for DOTS mitigation request: device with mac %s\n", node->mac);

                if (!spinconfig_dots_log_only()) {
                    // Add the node to the block list
                    // TODO: Do we need a better convenience function for internal 'rpc' calls?
                    char json_call[1024];
                    memset(json_call, 0, 1024);
                    snprintf(json_call, 1024, "{ \"jsonrpc\": \"2.0\", \"id\": 12345, \"method\": \"add_iplist_node\", \"params\": { \"list\": \"block\", \"node\": %d}}", node->id);
                    call_string_jsonrpc(json_call);
                    spin_log(LOG_INFO, "[dots] Device with mac %s was blocked due to DOTS mitigation request\n", node->mac);

                    //add_iplist_node(node_cache,
                }
            }
            flow_entry = tree_next(flow_entry);
        }

        cur = tree_next(cur);
    }
    spin_log(LOG_INFO, "[dots] Number of devices matching DOTS mitigation request: %d\n", devices_matching);
    return 0;
}
