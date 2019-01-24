/*
 * Copyright (c) 2018 Caspar Schutijser <caspar.schutijser@sidn.nl>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <err.h>
#include <stdint.h>
#include <stdbool.h>

#include <ldns/ldns.h>

#include "dns.h"
#include "util.h"

static void
print_dnsquery(const char *query, const char **ips, size_t ips_len,
    long long timestamp)
{
	size_t i;

	printf("{");
	print_str_str("command", "dnsquery");
	print_str_str("argument", "");
	printf("\"result\": { ");

	printf("\"queriednode\": { ");

	print_str_n("id", 1);
	print_str_n("lastseen", timestamp);
	printf("\"ips\": [ ");
	for (i = 0; i < ips_len; ++i) {
		printf("\"%s\"", ips[i]);
		if (i != ips_len-1) {
			printf(", ");
		}
	}
	printf(" ], "); // ips
	printf("\"domains\": [ ");
	// XXX ???
	printf("] "); // domains

	printf("}, "); // queriednode

	print_str_str("query", query);
	print_dummy_end();

	printf("} "); // result

	printf("}");
	printf("\n");
}

// XXX check more properties, such as IN, etc
// XXX cname results are ignored for now
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

	status = ldns_wire2pkt(&p, bp, length);
	if (status != LDNS_STATUS_OK) {
		DPRINTF("DNS: could not parse packet: %s",
		    ldns_get_errorstr_by_id(status));
		goto out;
	}

	count = ldns_rr_list_rr_count(ldns_pkt_question(p));
	if (count == 0) {
		DPRINTF("DNS: no owner?");
		goto out;
	} else if (count > 1) {
		DPRINTF("DNS: not supported: > 1 RR in question section");
		goto out;
	}

	answers = ldns_rr_list_new();
	ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_A,
	    LDNS_SECTION_ANSWER));
	ldns_rr_list_cat(answers, ldns_pkt_rr_list_by_type(p, LDNS_RR_TYPE_AAAA,
	    LDNS_SECTION_ANSWER));

	ips_len = ldns_rr_list_rr_count(answers);

	if (ips_len <= 0) {
		DPRINTF("DNS: no A or AAAA in answer section");
		goto out;
	}

	ips = calloc(ips_len, sizeof(char *));
	if (!ips)
		err(1, "calloc");

	query = ldns_rdf2str(ldns_rr_owner(ldns_rr_list_rr(ldns_pkt_question(p),
	    0)));
	if (!query) {
		DPRINTF("DNS: ldns_rdf2str failure");
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
			DPRINTF("DNS: ldns_rdf2str failure");
			goto out;
		}
		ips[i] = s;

		++i;
		rr = ldns_rr_list_pop_rr(answers);
	}

	print_dnsquery(query, (const char **)ips, ips_len, timestamp);

 out:
	for (i = 0; i < ips_len; ++i) {
		free(ips[i]);
	}
	free(ips);
	free(query);
	ldns_pkt_free(p);
}
