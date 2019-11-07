#include <sys/param.h>

#include <dirent.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>

#include "core2block.h"
#include "core2pubsub.h"
#include "ipl.h"
#include "rpc_json.h"
#include "spinhook.h"
#include "spin_log.h"
#include "statistics.h"


STAT_MODULE(spind)

tree_t *nodepair_tree = NULL;
tree_t *nodemap_tree = NULL;

/*
 * When adding RPC functions here, keep the following in mind:
 *
 * - The return type of *successful* calls is defined by the register
 *   function; e.g. int, string, complex, or none
 * - In case of an *error*, you must always set a String (svalue) to
 *   the result; this will be used in the error response; the return
 *   code of the rpc function will be the error code
 */


int addipnodefunc(void *cb_data, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    node_t *node;
    ip_t ipval;
    int nodenum;
    char *ipaddr;
    uint32_t timestamp;
    int new;
    node_cache_t* node_cache = (node_cache_t*) cb_data;

    nodenum = args[0].rpca_ivalue;
    ipaddr = args[1].rpca_svalue;

    timestamp = time(NULL);

    if (!spin_pton(&ipval, ipaddr)) {
        result->rpca_svalue = "Not a valid IP address";
        return -1;
    }
    node = node_cache_find_by_ip(node_cache, sizeof(ipval), &ipval);
    if (node) {
        result->rpca_svalue = "IP address already assigned to a node";
        return -1;
    }

    if (nodenum == 0) {
        node = node_create(0);
        node_set_modified(node, timestamp);
        node_add_ip(node, &ipval);
        new = node_cache_add_node(node_cache, node);
        assert(new);
    } else {
        node = node_cache_find_by_id(node_cache, nodenum);
        if (node == NULL) {
            result->rpca_svalue = "Unknown node id";
            return -1;
        }
        xnode_add_ip(node_cache, node, &ipval);
        node_set_modified(node, timestamp);
    }
    result->rpca_ivalue = node->id;
    return 0;
}

static node_t *
find_node_id(node_cache_t* node_cache, int node_id) {
    node_t *node;

    /*
     * Find it and give warning if non-existent. This should not happen.
     */
    node = node_cache_find_by_id(node_cache, node_id);
    if (node == NULL) {
        spin_log(LOG_WARNING, "Node-id %d not found!\n", node_id);
        return NULL;
    }
    return node;
}

/*
 *
 * Code to store and retrieve node info from files for persistency
 *
 */
static node_t*
decode_node_info(char *data, int datalen) {
    cJSON *node_json;
    node_t *newnode;
    cJSON *id_json, *mac_json, *name_json, *ips_json, *ip_json, *domains_json, *domain_json;

    newnode = 0;

    node_json = cJSON_Parse(data);
    if (node_json == NULL) {
        spin_log(LOG_ERR, "Unable to parse node data\n");
        goto end;
    }

    newnode = node_create(0);

    // id
    id_json = cJSON_GetObjectItemCaseSensitive(node_json, "id");
    if (!cJSON_IsNumber(id_json)) {
        spin_log(LOG_DEBUG, "No id in node\n");
        goto end;
    }
    newnode->id = id_json->valueint;

    // mac
    mac_json = cJSON_GetObjectItemCaseSensitive(node_json, "mac");
    if (!cJSON_IsString(mac_json)) {
        spin_log(LOG_DEBUG, "No mac in node\n");
    } else {
        spin_log(LOG_INFO, "[XX] NODE_SET_MAC 5\n");
        node_set_mac(newnode, mac_json->valuestring);
    }

    // name
    name_json = cJSON_GetObjectItemCaseSensitive(node_json, "name");
    if (!cJSON_IsString(name_json)) {
        spin_log(LOG_DEBUG, "No name in node\n");
    } else {
        node_set_name(newnode, name_json->valuestring);
    }

    // ips
    ips_json = cJSON_GetObjectItemCaseSensitive(node_json, "ips");
    if (!cJSON_IsArray(ips_json)) {
        spin_log(LOG_DEBUG, "No ips in node\n");
        goto end;
    }
    cJSON_ArrayForEach(ip_json, ips_json) {
        ip_t ipval;

        if (!cJSON_IsString(ip_json) || !spin_pton(&ipval, ip_json->valuestring)) {
            spin_log(LOG_DEBUG, "No valid ip in node\n");
            goto end;
        }
        node_add_ip(newnode, &ipval);
    }


    // domains
    domains_json = cJSON_GetObjectItemCaseSensitive(node_json, "domains");
    if (!cJSON_IsArray(domains_json)) {
        spin_log(LOG_DEBUG, "No domains in node\n");
        goto end;
    }
    cJSON_ArrayForEach(domain_json, domains_json) {
        if (!cJSON_IsString(ip_json)) {
            spin_log(LOG_DEBUG, "No valid domain in node\n");
            goto end;
        }
        node_add_domain(newnode, domain_json->valuestring);
    }

end:
    cJSON_Delete(node_json);
    return (node_t *) newnode;
}

static void
handle_node_info(node_cache_t* node_cache, char *buf, int size) {
    node_t *newnode;
    int oldid, newid;
    int wasnew;

    newnode = decode_node_info(buf, size);

    if (newnode) {
        oldid = newnode->id;
        newnode->id = 0;
        wasnew = node_cache_add_node(node_cache, newnode);
        // What if it was merged??
        assert(wasnew);
        newid = newnode->id;
        tree_add(nodemap_tree,
            sizeof(oldid), (void *) &oldid,
            sizeof(newid), (void *) &newid,
            1);
    }
}

#define NODE_READ_SIZE 10240


#define NODE_FILENAME_DIR "/etc/spin/nodestore"
#define NODEPAIRFILE "/etc/spin/nodepair.list"

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

static char node_filename[PATH_MAX];

static char *
node_filename_str(char *nodenum) {

    sprintf(node_filename, "%s/%s", NODE_FILENAME_DIR, nodenum);
    return node_filename;
}

/* TODO these need to go to lib */
static void
retrieve_node_info(node_cache_t* node_cache) {
    DIR *nodedir;
    struct dirent *fentry;
    char data[NODE_READ_SIZE];
    int fildes;
    int numbytes;
    char *filename;

    nodedir = opendir(NODE_FILENAME_DIR);
    if (nodedir == NULL) {
        spin_log(LOG_ERR, "Could not open directory %s\n", NODE_FILENAME_DIR);
        return;
    }
    while ((fentry = readdir(nodedir))!=NULL) {
        if (fentry->d_name[0] == '.') {
            continue;
        }
        filename = node_filename_str(fentry->d_name);
        fildes = open(filename, 0);
        if (fildes >= 0) {
            // do something with file
            // read it, decode it, remove it
            numbytes = read(fildes, data, NODE_READ_SIZE);
            // If it totally fills buffer it is too large
            if (numbytes < NODE_READ_SIZE) {
                // zero terminate, just in case
                data[numbytes] = 0;
                handle_node_info(node_cache, data, numbytes);
            }
            close(fildes);
        }
        unlink(filename);
    }
    closedir(nodedir);
}

static int map_node(int nodenum) {
    tree_entry_t *leaf;
    int newnodenum;

    leaf = tree_find(nodemap_tree, sizeof(nodenum), (void *) &nodenum);
    if (leaf == NULL) {
        spin_log(LOG_ERR, "COuld not find mapping for node %d\n", nodenum);
        return 0;
    }
    newnodenum = *((int *) leaf->data);
    return newnodenum;
}

int read_nodepair_tree(node_cache_t* node_cache, const char *filename) {
    int count = 0;
    char line[100];
    char* rline;
    int id[2];
    int node1, node2;
    int spinrpc_blockflow(node_cache_t* node_cache, int node1, int node2, int block);

    FILE* in = fopen(filename, "r");
    if (in == NULL) {
        return -1;
    }
    unlink(filename); // to prevent overwriting TODO
    while ((rline = fgets(line, sizeof(line), in)) != NULL) {
        if (sscanf(rline, "%d %d", &id[0], &id[1]) == 2) {
            node1 = map_node(id[0]);
            node2 = map_node(id[1]);
            spinrpc_blockflow(node_cache, node1, node2, 1);
            // Do mapping and actual blocking
            // tree_add(nodepair_tree, sizeof(id), (void *) id, 0, NULL, 1);
            count++;
        }
    }
    fclose(in);
    return count;
}
static void
init_blockflow(node_cache_t* node_cache) {

    retrieve_node_info(node_cache);

    read_nodepair_tree(node_cache, NODEPAIRFILE);

    tree_destroy(nodemap_tree);
    nodemap_tree = NULL;
}

void
cleanup_blockflow() {
    if (nodemap_tree != NULL) {
        tree_destroy(nodemap_tree);
    }
    if (nodepair_tree != NULL) {
        tree_destroy(nodepair_tree);
    }
}

static int
change_node_persistency(node_cache_t* node_cache, int nodenum, int val) {
    node_t *node;
    time_t now;

    node = node_cache_find_by_id(node_cache, nodenum);
    if (node != NULL) {
        if (node->persistent == 0) {
            // Is becoming persistent
            now = time(NULL);
            node_set_modified(node, now);
            c2b_node_persistent_start(nodenum);
        }
        node->persistent += val;
        if (node->persistent == 0) {
            // Stops being persistent
            now = time(NULL);
            node_set_modified(node, now);
            c2b_node_persistent_end(nodenum);
        }
        return 0;
    }
    return 1;
}

static void
store_nodepairs() {

    store_nodepair_tree(nodepair_tree, NODEPAIRFILE);
}


static int
spinrpc_blockflow_start(node_cache_t* node_cache, int node1, int node2) {
    int result;
    int node_ar[2];
    tree_entry_t *leaf;

    node_ar[0] = node1;
    node_ar[1] = node2;
    leaf = tree_find(nodepair_tree, sizeof(node_ar), (void *) node_ar);
    if (leaf != NULL) {
        // This flow was already blocked
        return 0;
    }

    result = 0;
    result += change_node_persistency(node_cache, node1, 1);
    result += change_node_persistency(node_cache, node2, 1);
    if (result) {
        return result;
    }

    // Copy flag is 1, key must be copied
    tree_add(nodepair_tree, sizeof(node_ar), (void *) node_ar, 0, NULL, 1);
    // new pair to block
    store_nodepairs();
    c2b_blockflow_start(node1, node2);
    /*
     * add device->node block flag if applicable
     */
    return 0;
}

static int
spinrpc_blockflow_stop(node_cache_t* node_cache, int node1, int node2) {
    int result;
    int node_ar[2];
    tree_entry_t *leaf;

    node_ar[0] = node1;
    node_ar[1] = node2;
    leaf = tree_find(nodepair_tree, sizeof(node_ar), (void *) node_ar);
    if (leaf == NULL) {
        // This flow was not blocked
        return 1;
    }
    // Remove from tree
    tree_remove_entry(nodepair_tree, leaf);
    store_nodepairs();
    c2b_blockflow_end(node1, node2);

    result = 0;
    result += change_node_persistency(node_cache, node1, -1);
    result += change_node_persistency(node_cache, node2, -1);
    return result;
}
int
spinrpc_blockflow(node_cache_t* node_cache, int node1, int node2, int block) {
    int result;
    int sn1, sn2;
    STAT_COUNTER(ctr, rpc-blockflow, STAT_TOTAL);
    node_t *n1, *n2;

    if (node1 == node2) {
        return 1;
    }
    n1 = find_node_id(node_cache, node1);
    n2 = find_node_id(node_cache, node2);

    if (n1 == NULL || n2 == NULL) {
        return 1;
    }
    if (node1 < node2) {
        sn1 = node1; sn2 = node2;
    } else {
        sn1 = node2; sn2 = node1;
    }
    if (block) {
        result = spinrpc_blockflow_start(node_cache, sn1, sn2);
    } else {
        result = spinrpc_blockflow_stop(node_cache, sn1, sn2);
    }
    STAT_VALUE(ctr, 1);
    if (n1->device) {
        spinhook_block_dev_node_flow(n1->device, n2, block);
    }
    if (n2->device) {
        spinhook_block_dev_node_flow(n2->device, n1, block);
    }

    return result;
}
rpc_arg_desc_t blockflow_args[] = {
    { "node1", RPCAT_INT },
    { "node2", RPCAT_INT },
    { "block", RPCAT_INT },
};
int blockflowfunc(void *cb_data, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    int res;
    node_cache_t* node_cache = (node_cache_t*) cb_data;

    res = spinrpc_blockflow(node_cache, args[0].rpca_ivalue, args[1].rpca_ivalue,args[2].rpca_ivalue);
    result->rpca_ivalue = res;
    return 0;
}


rpc_arg_desc_t devblockflow_args[] = {
    { "device", RPCAT_STRING },
    { "node", RPCAT_INT },
    { "block", RPCAT_INT },
};

int devblockflowfunc(void *cb_data, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    int res;
    node_t *devnode;
    node_cache_t* node_cache = (node_cache_t*)cb_data;

    devnode = node_cache_find_by_mac(node_cache, args[0].rpca_svalue);
    if (devnode == NULL) {
        // device not found
        result->rpca_svalue = "Device not found";
        return -1;
    }

    res = spinrpc_blockflow(node_cache, devnode->id, args[1].rpca_ivalue,args[2].rpca_ivalue);
    result->rpca_ivalue = res;
    return 0;
}


rpc_arg_desc_t devflow_args[] = {
    { "device", RPCAT_STRING },
};
int devflowfunc(void *cb_data, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    node_t *node;
    node_cache_t* node_cache = (node_cache_t*)cb_data;

    node = node_cache_find_by_mac(node_cache, args[0].rpca_svalue);
    if (node == NULL) {
        result->rpca_svalue = "Device not found";
        return -1;
    }
    result->rpca_cvalue = spin_data_flowlist(node);
    return 0;
}

rpc_arg_desc_t get_dev_data_args[] = {
    { "node", RPCAT_INT },
};

int
get_dev_data_func(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    node_t* node;
    int node_id = args[0].rpca_ivalue;
    node_cache_t* node_cache = (node_cache_t*)cb;

    node = node_cache_find_by_id(node_cache, node_id);
    if (node == NULL) {
        result->rpca_svalue = "Unknown node id";
        return -1;
    }
    result->rpca_cvalue = spin_data_node(node);
    return 0;
}

int
devlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    node_cache_t *node_cache = (node_cache_t *) cb;

    result->rpca_cvalue = spin_data_devicelist(node_cache);
    return 0;
}

int getblockflowfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    spin_data ar_sd;

    ar_sd = spin_data_nodepairtree(nodepair_tree);
    result->rpca_cvalue = ar_sd;
    return 0;
}



int
set_device_name_func(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    node_cache_t *node_cache = (node_cache_t *) cb;
    node_t *node;
    int node_id = args[0].rpca_ivalue;
    char* node_name = args[1].rpca_svalue;
    tree_entry_t* ip_entry;

    spin_log(LOG_DEBUG, "[XX] RPC SET NAME OF NODE %d TO %s\n", node_id, node_name);
    node = node_cache_find_by_id(node_cache, node_id);
    if (node == NULL) {
        spin_log(LOG_DEBUG, "[XX] NODE %d NOT FOUND\n", node_id);
        result->rpca_svalue = "Unknown node id";
        return -1;
    }
    // re-read node names, just in case someone has been editing it
    // TODO: make filename configurable? right now it will silently fail
    node_set_name(node, node_name);

    node_names_read_userconfig(node_cache->names, "/etc/spin/names.conf");
    // if it has a mac address, use that, otherwise, add for all its ip
    // addresses
    if (node->mac != NULL) {
        node_names_add_user_name_mac(node_cache->names, node->mac, node_name);
    } else {
        ip_entry = tree_first(node->ips);
        while (ip_entry != NULL) {
            node_names_add_user_name_ip(node_cache->names, (ip_t*)ip_entry->key, node_name);
            ip_entry = tree_next(ip_entry);
        }
    }

    node_names_write_userconfig(node_cache->names, "/etc/spin/names.conf");
    spin_log(LOG_DEBUG, "[XX] NAME SET\n");
    return 0;
}

rpc_arg_desc_t addipnode_args[] = {
    { "node", RPCAT_INT },
    { "ipaddr", RPCAT_STRING },
};
rpc_arg_desc_t set_device_name_args[] = {
    { "node", RPCAT_INT },
    { "name", RPCAT_STRING },
};

rpc_arg_desc_t iplist_addremove_node_args[] = {
    { "list", RPCAT_STRING },
    { "node", RPCAT_INT },
};
int update_iplist_node(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result, int add_remove) {
    // Four things to do:
    // - check for valid data
    // - add the ips of the node to the relevant list
    // - update the corresponding node in the node_cache
    // - call core2block to update the relevant firewall rules
    // Should we persist the list too? It is currently done automatically every 2.5 seconds
    int iplist_id, node_id;
    char* iplist_name;
    struct list_info* iplist;
    node_t* node;
    node_cache_t* node_cache = (node_cache_t*)cb;

    iplist_name = args[0].rpca_svalue;
    iplist_id = get_spin_iplist_id_by_name(iplist_name);
    if (iplist_id < 0) {
        result->rpca_svalue = "Unknown ip list name, should be 'ignore', 'block', or 'allow'";
        return -1;
    }
    iplist = get_spin_iplist(iplist_id);

    node_id = args[1].rpca_ivalue;
    node = node_cache_find_by_id(node_cache, node_id);
    if (node == NULL) {
        result->rpca_svalue = "Unknown node id";
        return -2;
    }

    // There is an add_ip_tree_to_li, but we need to loop over
    // the tree anyway to call c2b_changelist (no tree variant for
    // that one)
    tree_entry_t* entry = tree_first(node->ips);
    while (entry != NULL) {
        if (add_remove == SF_ADD) {
            add_ip_to_li(entry->key, iplist);
        } else {
            remove_ip_from_li(entry->key, iplist);
            //remove_ip_tree_from_li(node->ips, iplist);
        }
        c2b_changelist(NULL, iplist_id, add_remove, entry->key);
        entry = tree_next(entry);
    }
    node_cache_update_iplist_node(node_cache, iplist_id, add_remove, node_id);

    // Broadcast that the list was updated
    broadcast_iplist(iplist_id, iplist_name);

    return 0;
}

int add_iplist_node(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    return update_iplist_node(cb, args, result, SF_ADD);
}

int remove_iplist_node(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    return update_iplist_node(cb, args, result, SF_REM);
}


rpc_arg_desc_t iplist_addremove_ip_args[] = {
    { "list", RPCAT_STRING },
    { "ip", RPCAT_STRING },
};


int update_iplist_ip(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result, int add_remove) {
    // When we update a single IP, we do NOT update the node cache!
    // TODO: We may need to think about this. Choices:
    // 1. Do not update node_cache->on_list (like now)
    // 2. Update it (becomes inconsistent because it may have more ip addresses)
    // 3. Silently add all other ip addresses of that node as well

    int iplist_id;
    struct list_info* iplist;
    ip_t ip;
    char* iplist_name, * ip_str;

    // Check the arguments
    iplist_name = args[0].rpca_svalue;
    iplist_id = get_spin_iplist_id_by_name(iplist_name);
    if (iplist_id < 0) {
        result->rpca_svalue = "Unknown ip list name, should be 'ignore', 'block', or 'allow'";
        return -1;
    }
    iplist = get_spin_iplist(iplist_id);

    ip_str = args[1].rpca_svalue;
    if (!spin_pton(&ip, ip_str)) {
        result->rpca_svalue = "Invalid IP address";
        return -1;
    }

    // Update the internal list
    if (add_remove == SF_ADD) {
        add_ip_to_li(&ip, iplist);
    } else {
        remove_ip_from_li(&ip, iplist);
    }
    // Update firewall rules
    c2b_changelist(NULL, iplist_id, add_remove, &ip);

    // Broadcast that the list was updated
    broadcast_iplist(iplist_id, iplist_name);

    return 0;
}

int add_iplist_ip(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    return update_iplist_ip(cb, args, result, SF_ADD);
}

int remove_iplist_ip(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    return update_iplist_ip(cb, args, result, SF_REM);
}

rpc_arg_desc_t iplist_list_args[] = {
    { "list", RPCAT_STRING }
};
int list_iplist_ips(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    char* list_str;
    int iplist_id;
    struct list_info* iplist;

    list_str = args[0].rpca_svalue;
    iplist_id = get_spin_iplist_id_by_name(list_str);
    if (iplist_id < 0) {
        result->rpca_svalue = "Unknown ip list name, should be 'ignore', 'block', or 'allow'";
        return -1;
    }
    iplist = get_spin_iplist(iplist_id);

    result->rpca_cvalue = spin_data_ipar(iplist->li_tree);
    return 0;
}

/*
 * This command removes ALL items from the ignore list,
 * then resets it to a list of the local ip addresses of this computer
 * (through and external command, then reloading the list)
 */
int reset_iplist_ignore(void* cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    struct list_info* iplist = get_spin_iplist(IPLIST_IGNORE);

    // Remove whole tree, will be recreated
    tree_destroy(iplist->li_tree);

    system("rm /etc/spin/ignore.list");
    system("/usr/lib/spin/show_ips.lua -o /etc/spin/ignore.list -f");

    // Load the ignores again
    init_ipl(iplist);

    // Broadcast that the list was updated
    broadcast_iplist(IPLIST_IGNORE, iplist->li_listname);
    return 0;
}


/*
rpc_arg_desc_t list_member_args[] = {
    { "list", RPCAT_INT },
    { "addrem", RPCAT_INT },
    { "node", RPCAT_INT },
};
int spindlistfunc(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {

    handle_list_membership(args[0].rpca_ivalue, args[1].rpca_ivalue,args[2].rpca_ivalue);
    result->rpca_ivalue = 0;
    return 0;
}
*/



/*
int
list_rpc_calls_func(void *cb, rpc_arg_val_t *args, rpc_arg_val_t *result) {
    result->rpca_cvalue = rpc_list_registered_procedures();
    return 0;
}
*/


/*
 * Please note: make sure you specify the return value of the RPC method well;
 * In most cases it is probably just a status integer, in which case use RPCAT_INT, and
 * the wrappers here will create a JSON-RPC 2.0 response out of it.
 *
 * RPC method naming convention:
 * <operation>_<subject>[_<detail>]*
 * e.g.
 * list_devices
 * add_node_ip
 * set_flow_block
 * get_flow_block
 */
void
init_rpcs(node_cache_t *node_cache) {
    nodepair_tree = tree_create(cmp_2ints);
    // Make mapping tree while doing init
    nodemap_tree = tree_create(cmp_ints);
    init_blockflow(node_cache);

    rpc_register("node_add_ip", addipnodefunc, (void *) node_cache, 2, addipnode_args, RPCAT_NONE);
    rpc_register("set_flow_block", blockflowfunc, (void *) node_cache, 3, blockflow_args, RPCAT_NONE);
    rpc_register("set_device_flow_block", devblockflowfunc, (void *) node_cache, 3, devblockflow_args, RPCAT_NONE);
    rpc_register("get_flow_block", getblockflowfunc, (void *) 0, 0, 0, RPCAT_COMPLEX);
    rpc_register("get_device_data", get_dev_data_func, (void *) node_cache, 1, get_dev_data_args, RPCAT_COMPLEX);
    rpc_register("list_devices", devlistfunc, (void *) node_cache, 0, NULL, RPCAT_COMPLEX);
    rpc_register("list_device_flows", devflowfunc, (void *) node_cache, 1, devflow_args, RPCAT_COMPLEX);
    rpc_register("set_device_name", set_device_name_func, (void *) node_cache, 2, set_device_name_args, RPCAT_NONE);
    rpc_register("add_iplist_node", add_iplist_node, (void *) node_cache, 2, iplist_addremove_node_args, RPCAT_NONE);
    rpc_register("remove_iplist_node", remove_iplist_node, (void *) node_cache, 2, iplist_addremove_node_args, RPCAT_NONE);
    rpc_register("add_iplist_ip", add_iplist_ip,  (void *) node_cache, 2, iplist_addremove_ip_args, RPCAT_NONE);
    rpc_register("remove_iplist_ip", remove_iplist_ip,  (void *) node_cache, 2, iplist_addremove_ip_args, RPCAT_NONE);
    rpc_register("list_iplist", list_iplist_ips, 0, 1, iplist_list_args, RPCAT_COMPLEX);
    rpc_register("reset_iplist_ignore", reset_iplist_ignore, 0, 0, 0, RPCAT_NONE);

    register_internal_functions();
}



void
cleanup_rpcs() {
    cleanup_blockflow();
}
