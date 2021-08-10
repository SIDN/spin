#ifndef SPIND_RPC_CALLS_H
#define SPIND_RPC_CALLS_H 1

#include "node_cache.h"
#include "rpc_common.h"

/*
 * Manually add an IP address to an existing node
 * RPC name: node_ad_ip
 * Arguments:
 * node (int): the node ID
 * ipaddr (string): IP address (can be IPv4 or IPv6)
 */
int addipnodefunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

int blockflowfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

int devblockflowfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

int devflowfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);


/*
 * Retrieves a list of all 'local' devices (i.e. those with a known MAC
 * address
 * RPC Name: 'devicelist'
 * Arguments: none
 */
int devlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

int getblockflowfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

/*
 * Sets a name for a specific device (node)
 * RPC Name: 'set_device_name'
 * Arguments:
 * node (int): the node ID to set the name for
 * name (string): the name to set
 */
int set_device_name_func(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

/*
 * Read or modify one of the ignore, block en allow lists
 */
/* wait with this one, it uses many functions in spind itself
int spindlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);
rpc_arg_desc_t list_member_args[] = {
    { "list", RPCAT_INT },
    { "addrem", RPCAT_INT },
    { "node", RPCAT_INT },
};
*/
//int spindlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result);

void init_rpcs(node_cache_t *node_cache);
void cleanup_rpcs();

#endif //SPIND_RPC_CALLS_H
