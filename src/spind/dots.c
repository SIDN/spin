
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


int process_dots_signal(node_cache_t* node_cache, spin_data dots_message, char** error) {
    char* str;
    spin_log(LOG_DEBUG, "Processing DOTS signal\n");

    str = cJSON_Print(dots_message);
    spin_log(LOG_DEBUG, "Message: %s\n", str);
    free(str);
    spin_data mitigation_request = cJSON_GetObjectItemCaseSensitive(dots_message, "ietf-dots-signal-channel:mitigation-scope");

    if (mitigation_request == NULL) {
        *error = "ietf-dots-signal-channel:mitigation-scope missing";
        return 2;
    }

    *error = "Not implemented";
    return 1;
}
