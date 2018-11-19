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

#include <sys/types.h>
#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>
#ifdef __linux__
#include <net/ethernet.h>
#endif
#ifdef __OpenBSD__
#include <netinet/if_ether.h>
#endif

#include <stdio.h>

#include "util.h"

void
print_str_str(const char *a, const char *b)
{
	printf("\"%s\": \"%s\", ", a, b);
}

void
print_str_n(const char *a, long long d)
{
	printf("\"%s\": %lld, ", a, d);
}

void
print_fromto(const char *header, const char *mac, const char *ip)
{

	printf("\"%s\": { ", header);

	print_str_n("id", -1);
	if (mac)
		print_str_str("mac", mac);
	print_str_n("lastseen", 0);
	printf("\"ips\": [ \"%s\" ], ", ip);
	printf("\"domains\": [] ");

	printf("}, ");
}

void
print_dummy_end(void)
{
	printf("\"dummy\": 1 ");
}

int
mactostr(struct ether_addr *ea, char *s, size_t len)
{
	return snprintf(s, len, "%02x:%02x:%02x:%02x:%02x:%02x",
	    ea->ether_addr_octet[0], ea->ether_addr_octet[1],
	    ea->ether_addr_octet[2], ea->ether_addr_octet[3],
	    ea->ether_addr_octet[4], ea->ether_addr_octet[5]);
}

