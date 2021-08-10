#ifndef PROCESS_PKT_INFO_H
#define PROCESS_PKT_INFO_H 1

#include "node_cache.h"
#include "spinhook.h"

/*
 * process_pkt_info() is to be called from the layer that assembles pkt_info_t
 * structures from low-level information (gathered e.g. from Linux conntrack
 * or a PCAP file). Calling process_pkt_info() will insert the information
 * contained in the pkt_info_t into the SPIN daemon.
 */
void process_pkt_info(node_cache_t* node_cache, flow_list_t* flow_list, trafficfunc traffic_hook, int local_mode, pkt_info_t* pkt_info);

#endif
