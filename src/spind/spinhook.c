#include "node_cache.h"
#include "spinhook.h"
#include "spindata.h"
#include "core2pubsub.h"

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

