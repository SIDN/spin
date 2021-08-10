#ifndef DNSHOOKS_H
#define DNSHOOKS_H 1

#include <stdint.h>

#include "dns_cache.h"
#include "node_cache.h"
#include "pkt_info.h"

void dns_query_hook(dns_pkt_info_t *, int, uint8_t *);
void dns_answer_hook(dns_pkt_info_t *);
void dns_hooks_init(node_cache_t *, dns_cache_t *);

#endif
