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

    assert(node->device = 0);
    dev = malloc(sizeof(device_t));
    dev->dv_flowtree = tree_create(cmp_ints);
}

void
do_traffic(device_t *dev, node_t *node, int cnt, int bytes) {

    spin_log(LOG_DEBUG, "do_traffic to node %d (%d,%d)\n", node->id,cnt,bytes);
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
