
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
            //spin_data c_prefix = cJSON_GetArrayItem(target_prefix, j);
            const char* prefix = cJSON_GetArrayItem(target_prefix, j)->valuestring;
            spin_log(LOG_DEBUG, "Checking prefix: %s\n", prefix);
            // Loop over all devices' recent history entries
        }
    }

    *error = "Not implemented yet";
    return 1;
}
