#include <assert.h>

#include "node_cache.h"
#include "spinhook.h"
#include "spindata.h"
#include "core2pubsub.h"
#include "spin_log.h"

#include "statistics.h"

STAT_MODULE(spinhook)

void
spinhook_makedevice(node_t *node) {
    device_t *dev;

    spin_log(LOG_DEBUG, "Promote node %d to device", node->id);
    assert(node->device == 0);
    dev = (device_t *) malloc(sizeof(device_t));
    dev->dv_flowtree = tree_create(cmp_ints);
    dev->dv_nflows = 0;
    node->device = dev;
}

void
do_traffic(device_t *dev, node_t *node, int cnt, int bytes) {
    tree_entry_t *leaf;
    int nodeid = node->id;
    devflow_t *dfp;
    int *newint;
    STAT_COUNTER(ctr, traffic, STAT_TOTAL);

    // spin_log(LOG_DEBUG, "do_traffic to node %d (%d,%d)\n", node->id,cnt,bytes);
    leaf = tree_find(dev->dv_flowtree, sizeof(nodeid), &nodeid);
    STAT_VALUE(ctr, leaf!=NULL);
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
        dev->dv_nflows++;
        // Increase node reference count
        node->references++;
    } else {
        // spin_log(LOG_DEBUG, "Found existing devflow_t\n");
        dfp = (devflow_t *) leaf->data;
    }
    dfp->dvf_packets += cnt;
    dfp->dvf_bytes += bytes;
    dfp->dvf_activelastperiod = 1;
}

void
spinhook_traffic(node_t *src_node, node_t *dest_node, int packetcnt, int packetbytes) {
    int found = 0;

    if (src_node->device) {
        do_traffic(src_node->device, dest_node, packetcnt, packetbytes);
        found++;
    }
    if (dest_node->device) {
        do_traffic(dest_node->device, src_node, packetcnt, packetbytes);
        found++;
    }
    if (!found) {
        spin_log(LOG_ERR, "No device in %d to %d traffic\n", src_node->id, dest_node->id);
    }
}

spin_data spinhook_json(spin_data arg) {
    return arg;
}

static void
device_flow_remove(node_cache_t *node_cache, tree_t *ftree, tree_entry_t* leaf) {
    node_t *remnode;
    int remnodenum;
    STAT_COUNTER(ctr, flow-remove, STAT_TOTAL);

    remnodenum = * (int *) leaf->key;
    spin_log(LOG_DEBUG, "Remove flow to %d\n", remnodenum);

    remnode = node_cache_find_by_id(node_cache, remnodenum);
    assert(remnode != 0);
    remnode->references--;

    tree_remove_entry(ftree, leaf);

    STAT_VALUE(ctr, 1);
}

#define MAX_IDLE_PERIODS    10

static void
device_clean(node_cache_t *node_cache, node_t *node, void *ap) {
    device_t *dev;
    tree_entry_t *leaf, *nextleaf;
    int remnodenum;
    devflow_t *dfp;
    int removed = 0;
    STAT_COUNTER(ctr, device-clean, STAT_TOTAL);

    dev = node->device;
    assert(dev != NULL);

    spin_log(LOG_DEBUG, "Flows(%d) of node %d:\n", dev->dv_nflows, node->id);
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
                dev->dv_nflows--;
                removed++;
            }
        }

        leaf = nextleaf;
    }

    STAT_VALUE(ctr, removed);
}

void
spinhook_clean(node_cache_t *node_cache) {

    node_callback_devices(node_cache, device_clean, NULL);
}

static void
node_merge_flow(node_cache_t *node_cache, node_t *node, void *ap) {
    device_t *dev;
    tree_entry_t *srcleaf, *dstleaf;
    int *remnodenump;
    devflow_t *dfp, *destdfp;
    node_t *src_node, *dest_node;
    STAT_COUNTER(ctr, merge-flow, STAT_TOTAL);
    int *nodenumbers = (int *) ap;
    int srcnodenum, dstnodenum;

    dev = node->device;
    assert(dev != NULL);

    srcnodenum = nodenumbers[0];
    dstnodenum = nodenumbers[1];

    spin_log(LOG_DEBUG, "Renumber %d->%d in flows(%d) of node %d:\n", srcnodenum, dstnodenum, dev->dv_nflows, node->id);

    srcleaf = tree_find(dev->dv_flowtree, sizeof(srcnodenum), &srcnodenum);
     
    STAT_VALUE(ctr, srcleaf!= NULL);

    if (srcleaf == NULL) {
        // Nothing do to  here
        return;
    }

    // This flow must be renumbered
    spin_log(LOG_DEBUG, "Found entry\n");

    // Merge these two flow numbers if destination also in flowlist
    dstleaf = tree_find(dev->dv_flowtree, sizeof(dstnodenum), &dstnodenum);

    src_node = node_cache_find_by_id(node_cache, srcnodenum);
    assert(src_node != NULL);
    assert(src_node->references > 0);

    remnodenump = (int *) srcleaf->key;

    dfp = (devflow_t *) srcleaf->data;
    srcleaf->key = NULL;
    srcleaf->data = NULL;
    tree_remove_entry(dev->dv_flowtree, srcleaf);
    spin_log(LOG_DEBUG, "Removed old leaf\n");

    if (dstleaf != 0) {
        // Merge the numbers
        destdfp = (devflow_t *) dstleaf->data;
        destdfp->dvf_packets += dfp->dvf_packets;;
        destdfp->dvf_bytes += dfp->dvf_bytes;
        destdfp->dvf_idleperiods = 0;
        destdfp->dvf_activelastperiod = 1;

        free(remnodenump);
        free(dfp);

        dev->dv_nflows--;
    } else {
        // Reuse memory of key and data, renumber key
        dest_node = node_cache_find_by_id(node_cache, dstnodenum);
        assert(dest_node != NULL);

        *remnodenump = dstnodenum;
        tree_add(dev->dv_flowtree, sizeof(int), remnodenump, sizeof(devflow_t), dfp, 0);
        spin_log(LOG_DEBUG, "Added new leaf\n");
        dest_node->references++;
    }
    src_node->references--;
}

void
flows_merged(node_cache_t *node_cache, int node1, int node2) {
    int nodenumbers[2];

    nodenumbers[0] = node1;
    nodenumbers[1] = node2;
    node_callback_devices(node_cache, node_merge_flow, (void *) nodenumbers);
}

void
spinhook_nodedeleted(node_cache_t *node_cache, node_t *node) {
    spin_data command;
    spin_data sd;
    char mosqchan[100];

    sd = spin_data_node_deleted(node->id);
    command = spin_data_create_mqtt_command("nodeDeleted", NULL, sd);

    sprintf(mosqchan, "SPIN/admin");
    core2pubsub_publish_chan(mosqchan, command, 0);

    sprintf(mosqchan, "SPIN/traffic/node/%d", node->id);
    core2pubsub_publish_chan(mosqchan, NULL, 1);

    spin_data_delete(command);
}

void
spinhook_nodesmerged(node_cache_t *node_cache, node_t *dest_node, node_t *src_node) {
    spin_data command;
    spin_data sd;
    char mosqchan[100];

    sd = spin_data_nodes_merged(src_node->id, dest_node->id);
    command = spin_data_create_mqtt_command("nodeMerge", NULL, sd);

    sprintf(mosqchan, "SPIN/admin");
    core2pubsub_publish_chan(mosqchan, command, 0);

    spin_data_delete(command);

    sprintf(mosqchan, "SPIN/traffic/node/%d", src_node->id);
    core2pubsub_publish_chan(mosqchan, NULL, 1);

    flows_merged(node_cache, src_node->id, dest_node->id);
}

void
spinhook_init() {
}
