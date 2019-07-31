#include "rpc_json.h"
#include "rpc_common.h"
#include "node_cache.h"
#include "spin_log.h"
#include "statistics.h"
#include "core2block.h"
#include "spinhook.h"

#include <dirent.h>
#include <time.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>

STAT_MODULE(spind)

tree_t *nodepair_tree = NULL;
tree_t *nodemap_tree = NULL;


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
        result->rpca_ivalue = -1;
        return -1;
    }
    node = node_cache_find_by_ip(node_cache, sizeof(ipval), &ipval);
    if (node) {
        result->rpca_ivalue = -node->id;
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
            result->rpca_ivalue = -1;
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

static char node_filename[100];

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
        result->rpca_ivalue = -1;
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
        return 0;
    }
    result->rpca_cvalue = spin_data_flowlist(node);
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
        return 0;
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







void
init_rpcs(node_cache_t *node_cache) {
    nodepair_tree = tree_create(cmp_2ints);
    // Make mapping tree while doing init
    nodemap_tree = tree_create(cmp_ints);
    init_blockflow(node_cache);

    rpc_register("add_ip_to_node", addipnodefunc, (void *) node_cache, 2, addipnode_args, RPCAT_INT);
    rpc_register("blockflow", blockflowfunc, (void *) node_cache, 3, blockflow_args, RPCAT_INT);
    rpc_register("devblockflow", devblockflowfunc, (void *) node_cache, 3, devblockflow_args, RPCAT_INT);
    rpc_register("get_blockflow", getblockflowfunc, (void *) 0, 0, 0, RPCAT_COMPLEX);
    rpc_register("devicelist", devlistfunc, (void *) node_cache, 0, NULL, RPCAT_COMPLEX);
    rpc_register("get_deviceflow", devflowfunc, (void *) node_cache, 1, devflow_args, RPCAT_COMPLEX);
    rpc_register("set_device_name", set_device_name_func, (void *) node_cache, 2, set_device_name_args, RPCAT_COMPLEX);
}

void
cleanup_rpcs() {
    cleanup_blockflow();
}
