#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <string.h>
#include <errno.h>
#include <ldns/ldns.h>

#include "pkt_info.h"
#include "util.h"
#include "dns_cache.h"
#include "ipl.h"
#include "node_cache.h"
#include "core2nflog_dns.h"
#include "spin_log.h"
#include "spind.h"
#include "spinconfig.h"
#include "statistics.h"
#include "nflogroutines.h"

static node_cache_t* node_cache;
static dns_cache_t* dns_cache;

STAT_MODULE(dns)

// ip: source address of the query sender
// bp: query packet data
// length: query packet size
// timestamp: query time
// protocol: AF_INET or AF_INET6 (copied to dns_pkt_info)
void
handle_dns_query(const u_char *bp, u_int length, uint8_t* src_addr, int family, long long timestamp)
{
    ldns_status status;
    ldns_pkt *p = NULL;
    ldns_rdf* query_rdf;
    dns_pkt_info_t dns_pkt;
    size_t count;
    STAT_COUNTER(ctr, query, STAT_TOTAL);
    STAT_COUNTER(send, send-dns, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    status = ldns_wire2pkt(&p, bp, length);
    if (status != LDNS_STATUS_OK) {
        spin_log(LOG_WARNING, "DNS: could not parse packet: %s\n",
                 ldns_get_errorstr_by_id(status));
        phexdump(bp, length);
        goto out;
    }
    spin_log(LOG_DEBUG, "DNS query with qid: %u\n", ldns_pkt_id(p));

    count = ldns_rr_list_rr_count(ldns_pkt_question(p));
    if (count == 0) {
        spin_log(LOG_DEBUG, "DNS: no question section\n");
        goto out;
    } else if (count > 1) {
        spin_log(LOG_DEBUG, "DNS: not supported: > 1 RR in question section\n");
        goto out;
    }

    query_rdf = ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p), 0));

    dns_pkt.family = family;
    memcpy(dns_pkt.ip, src_addr, 16);
    dns_pkt.ttl = 0;
    // wireformat or string?
    // maybe convert now that we have access to lib?
    memcpy(dns_pkt.dname, query_rdf->_data, query_rdf->_size);

    node_cache_add_dns_query_info(node_cache, &dns_pkt, timestamp);
    // only send a notification if this node is not ignored (we
    // do cache it, should we not do that either?)
    if (!addr_in_ignore_list(family, src_addr)) {
        STAT_VALUE(send, 1);
        send_command_dnsquery(&dns_pkt);
    }

out:
    ldns_pkt_free(p);
}

void
handle_dns_answer(const u_char *bp, u_int length, long long timestamp, int protocol)
{
    ldns_status status;
    ldns_pkt *p = NULL;
    ldns_rr_list *answers = NULL, *tmp_list = NULL;
    ldns_rr *rr;
    ldns_rdf *rdf;
    size_t count;
    char *query = NULL;
    char *s;
    char **ips = NULL;
    size_t ips_len = 0;
    size_t i;
    STAT_COUNTER(ctr, answer, STAT_TOTAL);

    STAT_VALUE(ctr, 1);
    status = ldns_wire2pkt(&p, bp, length);
    if (status != LDNS_STATUS_OK) {
        spin_log(LOG_WARNING, "DNS: could not parse packet: %s\n",
                 ldns_get_errorstr_by_id(status));
        phexdump(bp, length);
        goto out;
    }
    spin_log(LOG_DEBUG, "DNS answer with qid: %u\n", ldns_pkt_id(p));

    count = ldns_rr_list_rr_count(ldns_pkt_question(p));
    if (count == 0) {
        spin_log(LOG_DEBUG, "DNS: no owner?\n");
        goto out;
    } else if (count > 1) {
        spin_log(LOG_DEBUG, "DNS: not supported: > 1 RR in question section\n");
        goto out;
    }

    answers = ldns_rr_list_new();

    tmp_list = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_A, LDNS_SECTION_ANSWER);
    if (tmp_list != NULL) {
		ldns_rr_list_cat(answers, tmp_list);
		ldns_rr_list_free(tmp_list);
	}
    tmp_list = ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_AAAA, LDNS_SECTION_ANSWER);
    if (tmp_list != NULL) {
		ldns_rr_list_cat(answers, tmp_list);
		ldns_rr_list_free(tmp_list);
	}

    ips_len = ldns_rr_list_rr_count(answers);

    if (ips_len <= 0) {
        spin_log(LOG_DEBUG, "DNS: no A or AAAA in answer section\n");
        goto out;
    }

    ips = calloc(ips_len, sizeof(char *));
    if (!ips) {
        spin_log(LOG_ERR, "error allocating memory (calloc)\n");
        goto out;
    }

    query = ldns_rdf2str(ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
        0)));
    if (!query) {
        spin_log(LOG_DEBUG, "DNS: ldns_rdf2str failure\n");
        goto out;
    }

    i = 0;
    rr = ldns_rr_list_pop_rr(answers);
    while (rr && i < ips_len) {
        // XXX TTL ldns_rr_ttl
        rdf = ldns_rr_rdf(rr, 0);
        s = ldns_rdf2str(rdf);
        if (!s) {
            spin_log(LOG_DEBUG, "DNS: ldns_rdf2str failure\n");
            goto out;
        }
        ips[i] = s;

        ldns_rdf* query_rdf = ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
        0));

        dns_pkt_info_t dns_pkt;
        if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_A) {
            dns_pkt.family = AF_INET;
            memset(dns_pkt.ip, 0, 12);
            memcpy(dns_pkt.ip+12, rdf->_data, rdf->_size);
        } else if (ldns_rr_get_type(rr) == LDNS_RR_TYPE_AAAA) {
            dns_pkt.family = AF_INET6;
            memcpy(dns_pkt.ip, rdf->_data, rdf->_size);
        } else {
            ips[i] = NULL;
            goto next;
        }
        memcpy(dns_pkt.dname, query_rdf->_data, query_rdf->_size);
        // TODO
        dns_pkt.ttl = 1234;
        time_t now = time(NULL);

        dns_cache_add(dns_cache, &dns_pkt, now);
        node_cache_add_dns_info(node_cache, &dns_pkt, now);

        next:
        ++i;
        ldns_rr_free(rr);
        rr = ldns_rr_list_pop_rr(answers);
    }

    for (i = 0; i < ips_len; ++i) {
        if (ips[i] != NULL) {
            spin_log(LOG_DEBUG, "DNS ANSWER: %s %s\n", query, ips[i]);
        }
    }

 out:
    for (i = 0; i < ips_len; ++i) {
        free(ips[i]);
    }
    free(ips);
    free(query);
    ldns_rr_list_deep_free(answers);
    ldns_pkt_free(p);
}


static void nflog_dns_callback(void* arg, int family, int protocol,
                            uint8_t* data, int size,
                            uint8_t* src_addr, uint8_t* dest_addr,
                            unsigned int src_port, unsigned int dest_port) {
    // skip udp header (all packets are udp atm)
    size_t header_size = 0;

    if (src_port == 53) {
        handle_dns_answer(data + header_size, size - header_size, 0, family);
    } else if (dest_port == 53) {
        time_t now = time(NULL);

        handle_dns_query(data + header_size, size - header_size, src_addr, family, now);
    }
}

void init_core2nflog_dns(node_cache_t* node_cache_a, dns_cache_t* dns_cache_a) {
    node_cache = node_cache_a;
    dns_cache = dns_cache_a;
    int nflog_dns_group;
    
    nflog_dns_group = spinconfig_iptable_nflog_dns_group();
    nflogroutine_register("core2nflog_dns", nflog_dns_callback, (void *) 0, nflog_dns_group);
}

void cleanup_core2nflog_dns() {
}
