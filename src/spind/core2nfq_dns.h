#ifndef CORE2NFQ_DNS
#define CORE2NGQ_DNS 1
#include "node_cache.h"
#include "dns_cache.h"

#define CORE2NFQ_DNS_QUEUE_NUMBER 1

// Initialize the core2nfq_dns module
// Arguments:
// node_cache_t* the global spin node cache
// node_cache_t* the global spin dns cache
//
// We may want to add some options here (such as a configurable queue number)
void init_core2nfq_dns(node_cache_t*, dns_cache_t*);
void cleanup_core2nfq_dns();

#endif
