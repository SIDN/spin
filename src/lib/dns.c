#include <ldns/ldns.h>

#include "node_cache.h"
#include "spin_log.h"

/*
 * This structure contains pointers to the callback functions that are to be
 * called from the handle_dns_query() and handle_dns_answer() functions.
 */
struct handle_dns_ctx {
    void (*query_hook)(dns_pkt_info_t *, int, uint8_t *);
    void (*answer_hook)(dns_pkt_info_t *);
};

// ctx: handle_dns_ctx structure
// ip: source address of the query sender
// bp: query packet data
// length: query packet size
// protocol: AF_INET or AF_INET6 (copied to dns_pkt_info)
void
handle_dns_query(const struct handle_dns_ctx *ctx, const u_char *bp, u_int length, uint8_t* src_addr, int family)
{
    ldns_status status;
    ldns_pkt *p = NULL;
    ldns_rdf* query_rdf;
    dns_pkt_info_t dns_pkt;
    size_t count;

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

    ctx->query_hook(&dns_pkt, dns_pkt.family, src_addr);

out:
    ldns_pkt_free(p);
}

void
handle_dns_answer(const struct handle_dns_ctx *ctx, const u_char *bp, u_int length, int protocol)
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

        ctx->answer_hook(&dns_pkt);

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

struct handle_dns_ctx *
handle_dns_init(void (*query_hook)(dns_pkt_info_t *, int, uint8_t *), void (*answer_hook)(dns_pkt_info_t *))
{
    struct handle_dns_ctx *ctx;

    ctx = malloc(sizeof(struct handle_dns_ctx));
    if (!ctx) {
        spin_log(LOG_ERR, "malloc: %s", strerror(errno));
        return NULL;
    }

    ctx->query_hook = query_hook;
    ctx->answer_hook = answer_hook;

    return ctx;
}

void
handle_dns_cleanup(struct handle_dns_ctx* ctx) {
    free(ctx);
}
