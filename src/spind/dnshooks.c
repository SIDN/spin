#include <time.h>

#include "dns_cache.h"
#include "ipl.h"
#include "node_cache.h"
#include "dnshooks.h"
#include "spind.h"
#include "statistics.h"

static node_cache_t* node_cache;
static dns_cache_t* dns_cache;

STAT_MODULE(dns)

void dns_query_hook(dns_pkt_info_t *dns_pkt, int family, uint8_t *src_addr)
{
    time_t now = time(NULL);
    STAT_COUNTER(ctr_query, query, STAT_TOTAL);
    STAT_COUNTER(send, send-dns, STAT_TOTAL);

    STAT_VALUE(ctr_query, 1);

    node_cache_add_dns_query_info(node_cache, dns_pkt, now);

    // only send a notification if this node is not ignored (we
    // do cache it, should we not do that either?)
    if (!addr_in_ignore_list(family, src_addr)) {
        STAT_VALUE(send, 1);
        send_command_dnsquery(dns_pkt);
    }
}

void dns_answer_hook(dns_pkt_info_t *dns_pkt)
{
    time_t now = time(NULL);
    STAT_COUNTER(ctr_answer, answer, STAT_TOTAL);

    STAT_VALUE(ctr_answer, 1);

    dns_cache_add(dns_cache, dns_pkt, now);
    node_cache_add_dns_info(node_cache, dns_pkt, now);
}

void dns_hooks_init(node_cache_t* node_cache_a, dns_cache_t* dns_cache_a)
{
    node_cache = node_cache_a;
    dns_cache = dns_cache_a;
}

