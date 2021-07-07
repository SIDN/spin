#ifndef CORE2NFLOG_DNS
#define CORE2NGLOG_DNS 1
#include "dns_cache.h"
#include "node_cache.h"

// Initialize the core2nflog_dns module
// Arguments:
// node_cache_t* the global spin node cache
// node_cache_t* the global spin dns cache
//
// We may want to add some options here (such as a configurable queue number)
int init_core2nflog_dns(node_cache_t*, dns_cache_t*);
void cleanup_core2nflog_dns();

#endif
