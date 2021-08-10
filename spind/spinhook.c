#include <assert.h>

#include "core2pubsub.h"
#include "spind.h"
#include "spin_log.h"
#include "statistics.h"

STAT_MODULE(spinhook)

devflow_t *spinhook_get_devflow(device_t *dev, node_t *node, int dst_port, int icmp_type) {
    tree_entry_t *leaf;
    int nodeid = node->id;
    devflow_t *dfp;
    devflow_key_t *new_flow_key;
    STAT_COUNTER(ctr, traffic, STAT_TOTAL);

    devflow_key_t find_flow_key;
    find_flow_key.dst_node_id = nodeid;
    find_flow_key.dst_port = dst_port;
    find_flow_key.icmp_type = icmp_type;

    leaf = tree_find(dev->dv_flowtree, sizeof(devflow_key_t), &find_flow_key);
    STAT_VALUE(ctr, leaf!=NULL);
    if (leaf == NULL) {
        spin_log(LOG_DEBUG, "Create new devflow_t\n");
        dfp = malloc(sizeof(devflow_t));
        dfp->dvf_blocked = 0;
        dfp->dvf_packets = 0;
        dfp->dvf_bytes = 0;
        dfp->dvf_lastseen = 0;
        dfp->dvf_idleperiods = 0;
        dfp->dvf_activelastperiod = 0;

        new_flow_key = malloc(sizeof(devflow_key_t));
        new_flow_key->dst_node_id = nodeid;
        new_flow_key->dst_port = dst_port;
        new_flow_key->icmp_type = icmp_type;

        // Add flow record indexed by destination nodeid, port, icmptype
        // Own the storage here
        tree_add(dev->dv_flowtree, sizeof(devflow_key_t), new_flow_key, sizeof(devflow_t), dfp, 0);
        dev->dv_nflows++;
        // Increase node reference count
        node->references++;
    } else {
        dfp = (devflow_t *) leaf->data;
    }
    return dfp;
}

void
spinhook_block_dev_node_flow(device_t *dev, node_t *node, int blocked, int dst_port, int icmp_type) {
    devflow_t *dfp;

    dfp = spinhook_get_devflow(dev, node, dst_port, icmp_type);
    dfp->dvf_blocked = blocked;
}

void
do_traffic(device_t *dev, node_t *node, int cnt, int bytes, uint32_t timestamp, int dst_port, int icmp_type) {
    devflow_t *dfp;

    dfp = spinhook_get_devflow(dev, node, dst_port, icmp_type);
    dfp->dvf_packets += cnt;
    dfp->dvf_bytes += bytes;
    dfp->dvf_lastseen = timestamp;
    dfp->dvf_activelastperiod = 1;
}

// Checks if there is a node with the given mac already, and if so,
// merge the two nodes, and return the pointer of the existing node
// If not, set the mac, and return the node pointer itself
static node_t*
check_for_existing_node_with_mac(node_cache_t* node_cache, node_t* node, char* mac) {
    node_t* result_node, * existing_node;
    // If there is a node with this mac already, merge this one into it,
    existing_node = node_cache_find_by_mac(node_cache, mac);
    if (existing_node != NULL) {
        if (existing_node != node) {
            merge_nodes(node_cache, node, existing_node);
        }
        // use the existing node now in this function
        result_node = existing_node;
    } else {
        node_set_mac(node, mac);
        cache_tree_remove_mac(node_cache, mac);
        cache_tree_add_mac(node_cache, node, mac);
        result_node = node;
    }
    // If it is not considered a device yet, make it one now
    if (result_node->device == NULL) {
        makedevice(result_node);
    }
    return result_node;
}

void
spinhook_traffic(node_cache_t *node_cache, node_t *src_node, node_t *dest_node, int packetcnt, int packetbytes, uint32_t timestamp, int dst_port, int icmp_type) {
    int found = 0;
    char *updated_mac = NULL;
    tree_entry_t* node_ip = NULL;
    tree_entry_t* next_node;

    if (src_node == dest_node) {
        // Probably internal stuff
        return;
    }
    if (!src_node->device && !dest_node->device) {
        // neither are known to be a device;
        // this may indicate we have outdated ARP information
        // Update it, and check again for both nodes
        spin_log(LOG_DEBUG, "No device in %d to %d traffic\n", src_node->id, dest_node->id);
        node_cache_update_arp(node_cache, timestamp);
        node_ip = tree_first(src_node->ips);
        while (node_ip) {
            next_node = tree_next(node_ip);
            updated_mac = arp_table_find_by_ip(node_cache->arp_table, node_ip->key);
            if (updated_mac) {
                src_node = check_for_existing_node_with_mac(node_cache, src_node, updated_mac);
            }
            node_ip = next_node;
        }
        node_ip = tree_first(dest_node->ips);
        while (node_ip) {
            next_node = tree_next(node_ip);
            updated_mac = arp_table_find_by_ip(node_cache->arp_table, node_ip->key);
            if (updated_mac) {
                dest_node = check_for_existing_node_with_mac(node_cache, dest_node, updated_mac);
            }
            node_ip = next_node;
        }
    }

    if (src_node->device) {
        do_traffic(src_node->device, dest_node, packetcnt, packetbytes, timestamp, dst_port, icmp_type);
        found++;
    }
    if (dest_node->device) {
        do_traffic(dest_node->device, src_node, packetcnt, packetbytes, timestamp, dst_port, icmp_type);
        found++;
    }
    // TODO: do we need to check yet again?
    if (!found) {
        spin_log(LOG_DEBUG, "STILL No device in %d to %d traffic\n", src_node->id, dest_node->id);

        // Probably ARP cache must be reread and acted upon here
        node_cache_update_arp(node_cache, timestamp);
    }
}

static void
device_flow_remove(node_cache_t *node_cache, tree_t *ftree, tree_entry_t* leaf) {
    node_t *remnode;
    int remnodenum;
    devflow_key_t flow_key;
    STAT_COUNTER(ctr, flow-remove, STAT_TOTAL);

    flow_key = *(devflow_key_t*) leaf->key;
    remnodenum = flow_key.dst_node_id;
    //remnodenum = * (int *) leaf->key;
    spin_log(LOG_DEBUG, "Remove flow to %d\n", remnodenum);

    remnode = node_cache_find_by_id(node_cache, remnodenum);
    if (remnode != NULL) {
        remnode->references--;
    }

    // This also frees key and data
    tree_remove_entry(ftree, leaf);

    STAT_VALUE(ctr, 1);
}

// The minimum number of flows that are remembered for a device
// (i.e. remote node, dst port, icmp_type combinations)
#define MIN_DEV_NEIGHBOURS  10
// Once more than MIN_DEV_NEIGHBOURS flows are stored,
// we remove them based on idle time. This is
// MAX_IDLE_PERIODS * CLEAN_TIMEOUT from spind (which is currently
// 15000 ms). To set it to, say half an hour, this value should be
// 120.
#define MAX_IDLE_PERIODS    120

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
            dfp->dvf_idleperiods = -1;
            dfp->dvf_activelastperiod = 0;
        }

        dfp->dvf_idleperiods++;
        if (dev->dv_nflows > MIN_DEV_NEIGHBOURS && dfp->dvf_idleperiods > MAX_IDLE_PERIODS) {
            device_flow_remove(node_cache, dev->dv_flowtree, leaf);
            dev->dv_nflows--;
            removed++;
        }

        leaf = nextleaf;
    }

    STAT_VALUE(ctr, removed);
}

void
spinhook_clean(node_cache_t *node_cache) {

    node_callback_devices(node_cache, device_clean, NULL);
}

void
node_merge_flow(node_cache_t *node_cache, node_t *node, void *ap) {
    device_t *dev;
    tree_entry_t *srcleaf, *dstleaf;
    devflow_key_t* flow_key;
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
    // We need to add every flow that has the source node number as a target
    // to the list of flows that have the destination node number
    // So we need to walk through the full list of flows of the source,
    // and add/update all flows on the target accordingly
    srcleaf = tree_first(dev->dv_flowtree);

    while (srcleaf != NULL) {

        dstleaf = tree_find(dev->dv_flowtree, sizeof(srcleaf->key), srcleaf->key);
        if (dstleaf) {

            // This flow must be renumbered

            // Merge these two flow numbers if destination also in flowlist
            dstleaf = tree_find(dev->dv_flowtree, sizeof(dstnodenum), &dstnodenum);

            src_node = node_cache_find_by_id(node_cache, srcnodenum);
            assert(src_node != NULL);
            assert(src_node->references > 0);

            flow_key = (devflow_key_t*) srcleaf->key;

            dfp = (devflow_t *) srcleaf->data;
            srcleaf->key = NULL;
            srcleaf->data = NULL;
            tree_remove_entry(dev->dv_flowtree, srcleaf);

            if (dstleaf != 0) {
                // Merge the numbers
                destdfp = (devflow_t *) dstleaf->data;
                destdfp->dvf_packets += dfp->dvf_packets;;
                destdfp->dvf_bytes += dfp->dvf_bytes;
                destdfp->dvf_idleperiods = 0;
                destdfp->dvf_activelastperiod = 1;

                free(flow_key);
                free(dfp);

                dev->dv_nflows--;
            } else {
                // Reuse memory of key and data, renumber key, add it to the tree
                dest_node = node_cache_find_by_id(node_cache, dstnodenum);
                assert(dest_node != NULL);

                devflow_key_t* dst_flow_key = flow_key;
                dst_flow_key->dst_node_id = dstnodenum;
                tree_add(dev->dv_flowtree, sizeof(int), dst_flow_key, sizeof(devflow_t), dfp, 0);
                spin_log(LOG_DEBUG, "Added new leaf\n");
                dest_node->references++;
            }
            src_node->references--;
        }

        STAT_VALUE(ctr, srcleaf != NULL);
        srcleaf = tree_next(srcleaf);
    }

}

void
flows_merged(node_cache_t *node_cache, int node1, int node2) {
    int nodenumbers[2];

    nodenumbers[0] = node1;
    nodenumbers[1] = node2;
    node_callback_devices(node_cache, node_merge_flow, (void *) nodenumbers);
}

static char *adminchannel = "SPIN/traffic/admin";

void
spinhook_nodedeleted(node_cache_t *node_cache, node_t *node) {
    spin_data command;
    spin_data sd;
    char mosqchan[100];

    publish_nodes();
    sd = spin_data_node_deleted(node->id);

    command = spin_data_create_mqtt_command("nodeDeleted", NULL, sd);
    core2pubsub_publish_chan(adminchannel, command, 0);
    spin_data_delete(command);

    sprintf(mosqchan, "SPIN/traffic/node/%d", node->id);
    core2pubsub_publish_chan(mosqchan, NULL, 1);
}

void
spinhook_nodesmerged(node_cache_t *node_cache, node_t *dest_node, node_t *src_node) {
    spin_data command;
    spin_data sd;
    char mosqchan[100];

    publish_nodes();
    sd = spin_data_nodes_merged(src_node->id, dest_node->id);

    command = spin_data_create_mqtt_command("nodeMerged", NULL, sd);
    core2pubsub_publish_chan(adminchannel, command, 0);
    spin_data_delete(command);

    sprintf(mosqchan, "SPIN/traffic/node/%d", src_node->id);
    core2pubsub_publish_chan(mosqchan, NULL, 1);

    flows_merged(node_cache, src_node->id, dest_node->id);
}
