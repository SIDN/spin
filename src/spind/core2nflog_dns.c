#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <string.h>
#include <errno.h>

#include "dns.h"
#include "dnshooks.h"
#include "pkt_info.h"
#include "util.h"
#include "dns_cache.h"
#include "node_cache.h"
#include "core2nflog_dns.h"
#include "spin_log.h"
#include "spinconfig.h"
#include "nflogroutines.h"

static struct handle_dns_ctx *handle_dns_ctx;

static void nflog_dns_callback(void* arg, int family, int protocol,
                            uint8_t* data, int size,
                            uint8_t* src_addr, uint8_t* dest_addr,
                            unsigned int src_port, unsigned int dest_port) {
    // skip udp header (all packets are udp atm)
    size_t header_size = 0;

    if (src_port == 53) {
        handle_dns_answer(handle_dns_ctx, data + header_size, size - header_size, family);
    } else if (dest_port == 53) {
        handle_dns_query(handle_dns_ctx, data + header_size, size - header_size, src_addr, family);
    }
}

void init_core2nflog_dns(node_cache_t* node_cache, dns_cache_t* dns_cache) {
    int nflog_dns_group;

    dns_hooks_init(node_cache, dns_cache);

    handle_dns_ctx = handle_dns_init(&dns_query_hook, &dns_answer_hook);
    if (handle_dns_ctx == NULL) {
        spin_log(LOG_ERR, "handle_dns_init failure");
        exit(1);
    }

    nflog_dns_group = spinconfig_iptable_nflog_dns_group();
    nflogroutine_register("core2nflog_dns", nflog_dns_callback, (void *) 0, nflog_dns_group);
}

void cleanup_core2nflog_dns() {
}
