
#include "dns.h"
#include "dnshooks.h"
#include "nflogroutines.h"
#include "spin_config.h"
#include "spin_log.h"

static struct handle_dns_ctx *handle_dns_ctx = NULL;

static void nflog_dns_callback(void* arg, int family, int protocol,
                            uint8_t* data, int size,
                            uint8_t* src_addr, uint8_t* dest_addr,
                            unsigned int src_port, unsigned int dest_port) {
    // skip udp header (all packets are udp atm)
    size_t header_size = 0;

    spin_log(LOG_DEBUG, "DNS callback for packet src port %u dst port %u\n", src_port, dest_port);

    if (src_port == 53) {
        handle_dns_answer(handle_dns_ctx, data + header_size, size - header_size, family);
    } else if (dest_port == 53) {
        handle_dns_query(handle_dns_ctx, data + header_size, size - header_size, src_addr, family);
    }
}

int init_core2nflog_dns(node_cache_t* node_cache, dns_cache_t* dns_cache) {
    int nflog_dns_group;
    int result;

    handle_dns_ctx = handle_dns_init(&dns_query_hook, &dns_answer_hook);
    if (handle_dns_ctx == NULL) {
        spin_log(LOG_ERR, "handle_dns_init failure");
        return 1;
    }

    nflog_dns_group = spinconfig_iptable_nflog_dns_group();
    result = nflogroutine_register("core2nflog_dns", nflog_dns_callback, (void *) 0, nflog_dns_group);
    if (result != 0) {
        spin_log(LOG_ERR, "core2nflog_dns initialization failed");
    }
    return result;
}

void cleanup_core2nflog_dns() {
    nflogroutine_close("core2nflog_dns");
    if (handle_dns_ctx != NULL) {
        handle_dns_cleanup(handle_dns_ctx);
    }
}
