#ifndef SPIN_CORE2BLOCK_H
#define SPIN_CORE2BLOCK_H 1

#include "util.h"
#include "node_cache.h"

void init_core2block(int passive_mode);
void cleanup_core2block();

void c2b_changelist(void* arg, int iplist, int add, ip_t *ip_addr);
void c2b_node_persistent_start(int nodenum);
void c2b_node_persistent_end(int nodenum);
void c2b_node_ipaddress(int nodenum, ip_t *ip_addr);
void c2b_blockflow_start(int nodenum1, int nodenum2);
void c2b_blockflow_end(int nodenum1, int nodenum2);

#endif
