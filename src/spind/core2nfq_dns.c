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

static struct nfq_handle* dns_qh = NULL;
static struct nfq_q_handle* dns_q_qh = NULL;
// todo: pass this from main spind.c
static node_cache_t* node_cache;
static dns_cache_t* dns_cache;

void phexdump(const uint8_t* data, unsigned int size) {
    unsigned int i;
    printf("00: ");
    for (i = 0; i < size; i++) {
        if (i > 0 && i % 10 == 0) {
            printf("\n%u: ", i);
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}


void
handle_dns(const u_char *bp, u_int length, long long timestamp)
{
    ldns_status status;
    ldns_pkt *p = NULL;
    ldns_rr_list *answers;
    ldns_rr *rr;
    ldns_rdf *rdf;
#if unused
    ldns_rr_type type;
#endif
    size_t count;
    char *query = NULL;
    char *s;
    char **ips = NULL;
    size_t ips_len = 0;
    size_t i;

    phexdump(bp, length);
    status = ldns_wire2pkt(&p, bp, length);
    if (status != LDNS_STATUS_OK) {
        printf("DNS: could not parse packet: %s\n",
            ldns_get_errorstr_by_id(status));
        goto out;
    }

    count = ldns_rr_list_rr_count(ldns_pkt_question(p));
    if (count == 0) {
        printf("DNS: no owner?\n");
        goto out;
    } else if (count > 1) {
        printf("DNS: not supported: > 1 RR in question section\n");
        goto out;
    }

    answers = ldns_rr_list_new();
    ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_A,
        LDNS_SECTION_ANSWER));
    ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_AAAA,
        LDNS_SECTION_ANSWER));

    ips_len = ldns_rr_list_rr_count(answers);

    if (ips_len <= 0) {
        printf("DNS: no A or AAAA in answer section\n");
        goto out;
    }

    ips = calloc(ips_len, sizeof(char *));
    if (!ips) {
        fprintf(stderr, "calloc");
        goto out;
    }

    query = ldns_rdf2str(ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
        0)));
    if (!query) {
        printf("DNS: ldns_rdf2str failure\n");
        goto out;
    }

    i = 0;
    rr = ldns_rr_list_pop_rr(answers);
    while (rr && i < ips_len) {
#if unused
        type = ldns_rr_get_type(rr);
#endif

        // XXX TTL ldns_rr_ttl
        rdf = ldns_rr_rdf(rr, 0);
        s = ldns_rdf2str(rdf);
        if (!s) {
            printf("DNS: ldns_rdf2str failure\n");
            goto out;
        }
        ips[i] = s;

        ip_t ip;

        ldns_rdf* query_rdf = ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
        0));


        dns_pkt_info_t dns_pkt;
        dns_pkt.family = AF_INET;
        memset(dns_pkt.ip, 0, 12);
        memcpy(dns_pkt.ip+12, rdf->_data, rdf->_size);
        memcpy(dns_pkt.dname, query_rdf->_data, query_rdf->_size);
        // TODO
        dns_pkt.ttl = 1234;
        time_t now = time(NULL);

        if (rdf->_size == 4) {
            printf("[XX] ADD IPV4 ANSWER TO DNS CACHE:\n");
            char pktinfo_str[2048];
            dns_pktinfo2str(pktinfo_str, &dns_pkt, 2048);
            printf("[XX] PKTINFO: %s\n", pktinfo_str);

            dns_cache_add(dns_cache, &dns_pkt, now);
            node_cache_add_dns_info(node_cache, &dns_pkt, now);
        } else {
            printf("[XX] ADD IPV6 ANSWER TO DNS CACHE\n");
            dns_cache_add(dns_cache, &dns_pkt, now);
            node_cache_add_dns_info(node_cache, &dns_pkt, now);
        }

        ++i;
        rr = ldns_rr_list_pop_rr(answers);

    }
    for (i = 0; i < ips_len; ++i) {
        printf("[XX] DNS ANSWER: %s %s\n", query, ips[i]);
    }

    //print_dnsquery(query, (const char **)ips, ips_len, timestamp);

 out:
    for (i = 0; i < ips_len; ++i) {
        free(ips[i]);
    }
    free(ips);
    free(query);
    ldns_pkt_free(p);
}

u_int32_t treat_pkt(struct nfq_data *nfa) {
    int id = 0;
    struct nfqnl_msg_packet_hdr *ph;
    unsigned char* data;
    int ret;

    ph = nfq_get_msg_packet_hdr(nfa);
    if (ph) {
        id = ntohl(ph->packet_id);
        ret = nfq_get_payload(nfa, &data);
        if (ret >= 28) {
            handle_dns(data+28, ret-28, 0);
        }
    }
    return id;
}

static int dns_cap_cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
              struct nfq_data *nfa, void *data)
{
    printf("[XX] dns cb called\n");
    u_int32_t id = treat_pkt(nfa); /* Treat packet */
    return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

// callback code when there is nfq data

/*
            //printf("[XX] check nfq\n");
            if ((fds[1].revents & POLLIN)) {
                //printf("[XX] nfq has data\n");
                if ((dns_q_rv = recv(dns_q_fd, dns_q_buf, sizeof(dns_q_buf), 0)) >= 0) {
                    // TODO we should add this one to the poll part
                    // now this is slow and once per loop is not enough
                    nfq_handle_packet(dns_qh, dns_q_buf, dns_q_rv); // send packet to callback
                }
                //printf("[XX] rest of loop\n");
            }
*/

void init_core2nfq_dns() {
    // TODO: set up nfq here or in startup script?
    dns_qh = nfq_open();
    if (!dns_qh) {
        fprintf(stderr, "error during nfq_open()\n");
        exit(1);
    }
    dns_q_qh = nfq_create_queue(dns_qh,  0, &dns_cap_cb, NULL);
    if (dns_q_qh == NULL) {
        fprintf(stderr, "unable to create Netfilter Queue: %s\n", strerror(errno));
        exit(1);
    }
    printf("[XX] DNS_Q_QH: %p\n", dns_q_qh);
    if (nfq_set_mode(dns_q_qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
        fprintf(stderr, "can't set packet_copy mode\n");
        exit(1);
    }
    // for register
    int dns_q_fd = nfq_fd(dns_qh);
    char dns_q_buf[4096] __attribute__ ((aligned));
    int dns_q_rv;

}

void cleanup_core2nfq_dns() {
    nfq_destroy_queue(dns_q_qh);
    nfq_close(dns_qh);
}
