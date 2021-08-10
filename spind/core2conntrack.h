#ifndef CORE2CONNTRACK
#define CORE2CONNTRACK 1
#include "node_cache.h"
#include "process_pkt_info.h"
#include "spinhook.h"

// Initialize the core2conntrack module
// Arguments:
// node_cache_t* node_cache: the global spin node cache
// int local_mode: set to 1 to not ignore this device's data
//
// We may want to add some options here (such as a configurable queue number)

int init_core2conntrack(node_cache_t* node_cache, int local_mode, trafficfunc);
void cleanup_core2conntrack();

#endif
