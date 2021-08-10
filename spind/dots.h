#ifndef SPIN_DOTS_H
#define SPIN_DOTS_H 1

#include "rpc_common.h"
#include "node_cache.h"

int process_dots_signal(node_cache_t* node_cache, spin_data dots_message, char** error);

#endif
