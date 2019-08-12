#ifndef SPIN_CORE2KERNEL_H
#define SPIN_CORE2KERNEL_H 1

#include "node_cache.h"
#include "spin_cfg.h"
#include "util.h"

int init_netlink(int, node_cache_t*);
void cleanup_cache();
void cleanup_netlink();

int core2kernel_do(config_command_t);
int core2kernel_do_ip(config_command_t, ip_t*);

#endif // SPIN_CORE2KERNEL_H
