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
#include "node_cache.h"
#include "core2nfq_dns.h"
#include "spin_log.h"

static struct nfq_handle* dns_qh = NULL;
static struct nfq_q_handle* dns_q_qh = NULL;
static node_cache_t* node_cache;
static dns_cache_t* dns_cache;
static int dns_q_fd;

void
handle_dns(const u_char *bp, u_int length, long long timestamp, int protocol)
{
    ldns_status status;
    ldns_pkt *p = NULL;
    ldns_rr_list *answers;
    ldns_rr *rr;
    ldns_rdf *rdf;
    size_t count;
    char *query = NULL;
    char *s;
    char **ips = NULL;
    size_t ips_len = 0;
    size_t i;

    status = ldns_wire2pkt(&p, bp, length);
    if (status != LDNS_STATUS_OK) {
        spin_log(LOG_WARNING, "DNS: could not parse packet: %s\n",
                 ldns_get_errorstr_by_id(status));
        goto out;
    }

    count = ldns_rr_list_rr_count(ldns_pkt_question(p));
    if (count == 0) {
        spin_log(LOG_DEBUG, "DNS: no owner?\n");
        goto out;
    } else if (count > 1) {
        spin_log(LOG_DEBUG, "DNS: not supported: > 1 RR in question section\n");
        goto out;
    }

    answers = ldns_rr_list_new();
    ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_A,
        LDNS_SECTION_ANSWER));
    ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_AAAA,
        LDNS_SECTION_ANSWER));

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

        ip_t ip;

        ldns_rdf* query_rdf = ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
        0));


        dns_pkt_info_t dns_pkt;
        dns_pkt.family = protocol;
        if (protocol == AF_INET) {
            memset(dns_pkt.ip, 0, 12);
            memcpy(dns_pkt.ip+12, rdf->_data, rdf->_size);
        } else {
            memcpy(dns_pkt.ip, rdf->_data, rdf->_size);
        }
        memcpy(dns_pkt.dname, query_rdf->_data, query_rdf->_size);
        // TODO
        dns_pkt.ttl = 1234;
        time_t now = time(NULL);

        if (rdf->_size == 4) {
            char pktinfo_str[2048];
            dns_pktinfo2str(pktinfo_str, &dns_pkt, 2048);

            dns_cache_add(dns_cache, &dns_pkt, now);
            node_cache_add_dns_info(node_cache, &dns_pkt, now);
        } else {
            dns_cache_add(dns_cache, &dns_pkt, now);
            node_cache_add_dns_info(node_cache, &dns_pkt, now);
        }
        send_command_dnsquery(&dns_pkt);

        ++i;
        rr = ldns_rr_list_pop_rr(answers);

    }
    for (i = 0; i < ips_len; ++i) {
        spin_log(LOG_DEBUG, "DNS ANSWER: %s %s\n", query, ips[i]);
    }

 out:
    for (i = 0; i < ips_len; ++i) {
        free(ips[i]);
    }
    free(ips);
    free(query);
    ldns_pkt_free(p);
}

static int nfq_dns_callback(void* arg, int protocol, void* data, size_t size) {
    handle_dns(data, size, 0, protocol);
    return 1;
}

void init_core2nfq_dns(node_cache_t* node_cache_a, dns_cache_t* dns_cache_a) {
    node_cache = node_cache_a;
    dns_cache = dns_cache_a;
    
    nfqroutine_register("core2block", nfq_dns_callback, (void *) 0, CORE2NFQ_DNS_QUEUE_NUMBER);
}

void cleanup_core2nfq_dns() {
}
