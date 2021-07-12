#ifndef CORE2EXTSRC_H
#define CORE2EXTSRC_H 1
#include "dns_cache.h"
#include "node_cache.h"
#include "spinhook.h"

int init_core2extsrc(node_cache_t *, dns_cache_t *, trafficfunc, char *, char *);
void cleanup_core2extsrc();

#endif
