#include <assert.h>

#include "node_cache.h"
#include "spinhook.h"
#include "spindata.h"
#include "core2pubsub.h"
#include "spin_log.h"

void
spinhook_nodesmerged(node_t *dest_node, node_t *src_node) {
    spin_data command;
    spin_data sd;
    char mosqchan[100];

    sd = spin_data_merge(src_node->id, dest_node->id);
    command = spin_data_create_mqtt_command("nodeMerge", NULL, sd);

    sprintf(mosqchan, "SPIN/traffic/node/%d", dest_node->id);
    core2pubsub_publish_chan(mosqchan, command, 1);

    spin_data_delete(command);
}

void
spinhook_makedevice(node_t *node) {
    device_t *dev;

    assert(node->device == 0);
    dev = malloc(sizeof(device_t));
    dev->dv_flowtree = tree_create(cmp_ints);
    node->device = dev;
}

void
do_traffic(device_t *dev, node_t *node, int cnt, int bytes) {
    tree_entry_t *leaf;
    int nodeid = node->id;
    devflow_t *dfp;
    int *newint;

    spin_log(LOG_DEBUG, "do_traffic to node %d (%d,%d)\n", node->id,cnt,bytes);
    leaf = tree_find(dev->dv_flowtree, sizeof(nodeid), &nodeid);
    if (leaf == NULL) {
        spin_log(LOG_DEBUG, "Create new devflow_t\n");
        dfp = malloc(sizeof(devflow_t));
        dfp->dvf_packets = 0;
        dfp->dvf_bytes = 0;
        dfp->dvf_idleperiods = 0;
        dfp->dvf_activelastperiod = 0;

        newint = malloc(sizeof(int));
        *newint = nodeid;
        // Add flow record indexed by destination nodeid
        // Own the storage here
        tree_add(dev->dv_flowtree, sizeof(int), newint, sizeof(devflow_t), dfp, 0);
        // Increase node reference count
        node->references++;
    } else {
        spin_log(LOG_DEBUG, "Found existing devflow_t\n");
        dfp = (devflow_t *) leaf->data;
    }
    dfp->dvf_packets += cnt;
    dfp->dvf_bytes += bytes;
    dfp->dvf_activelastperiod = 1;
    spin_log(LOG_DEBUG, "dvf: %d %d %d %d\n",
        dfp->dvf_packets, dfp->dvf_bytes,
        dfp->dvf_idleperiods, dfp->dvf_activelastperiod);
}

void
spinhook_traffic(node_t *src_node, node_t *dest_node, int packetcnt, int packetbytes) {

    spin_log(LOG_DEBUG, "Traffic %d->%d (%d, %d)\n",
                src_node->id, dest_node->id, packetcnt, packetbytes);
    if (src_node->device) {
        do_traffic(src_node->device, dest_node, packetcnt, packetbytes);
    }
    if (dest_node->device) {
        do_traffic(dest_node->device, src_node, packetcnt, packetbytes);
    }
}

spin_data spinhook_json(spin_data arg) {
    return arg;
}

static void
device_flow_remove(node_cache_t *node_cache, tree_t *ftree, tree_entry_t* leaf) {
    node_t *remnode;
    int remnodenum;

    remnodenum = * (int *) leaf->key;
    spin_log(LOG_DEBUG, "Remove flow to %d\n", remnodenum);

    remnode = node_cache_find_by_id(node_cache, remnodenum);
    assert(remnode != 0);
    remnode->references--;

    tree_remove_entry(ftree, leaf);
}

#define MAX_IDLE_PERIODS    10

static void
device_clean(node_cache_t *node_cache, node_t *node) {
    device_t *dev;
    tree_entry_t *leaf, *nextleaf;
    int remnodenum;
    devflow_t *dfp;

    dev = node->device;
    if (dev == NULL)
        return;
    spin_log(LOG_DEBUG, "Flows of node %d:\n", node->id);
    leaf = tree_first(dev->dv_flowtree);
    while (leaf != NULL) {
        nextleaf = tree_next(leaf);
        remnodenum = * (int *) leaf->key;
        dfp = (devflow_t *) leaf->data;
        spin_log(LOG_DEBUG, "to node %d: %d %d %d %d\n", remnodenum,
            dfp->dvf_packets, dfp->dvf_bytes,
            dfp->dvf_idleperiods, dfp->dvf_activelastperiod);

        if (dfp->dvf_activelastperiod) {
            dfp->dvf_idleperiods = 0;
            dfp->dvf_activelastperiod = 0;
        } else {
            dfp->dvf_idleperiods++;
            if (dfp->dvf_idleperiods <= MAX_IDLE_PERIODS) {
                dfp->dvf_activelastperiod = 0;
            } else {
                device_flow_remove(node_cache, dev->dv_flowtree, leaf);
            }
        }

        leaf = nextleaf;
    }
}

void
spinhook_clean(node_cache_t *node_cache) {

    node_callback_devices(node_cache, device_clean);
}

void
spinhook_init() {
}
