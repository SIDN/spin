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

#ifdef HAVE_SYS_TREE_H
#include <sys/tree.h>
#else
#include "external/tree.h"
#endif

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "arp_table.h"

struct node {
	RB_ENTRY(node) entry;
	char *ip;
	char *mac;
};

struct arp_table {
	RB_HEAD(arp_tree, node) tree;
};

static int
nodecmp(struct node *a, struct node *b)
{
	return strcmp(a->ip, b->ip);
}

RB_GENERATE_STATIC(arp_tree, node, entry, nodecmp);

struct arp_table *
arp_table_create(void)
{
	struct arp_table *t;

	t = malloc(sizeof(struct arp_table));
	if (!t)
		err(1, "malloc");

	RB_INIT(&t->tree);

	return t;
}

void
arp_table_destroy(struct arp_table *arp_table)
{
	struct node *n, *nxt;

	for (n = RB_MIN(arp_tree, &arp_table->tree); n != NULL; n = nxt) {
		nxt = RB_NEXT(arp_tree, &arp_table->tree, n);
		RB_REMOVE(arp_tree, &arp_table->tree, n);

		free(n->ip);
		free(n->mac);
		free(n);
	}

	free(arp_table);
}

void
arp_table_add(struct arp_table *arp_table, char *ip, char *mac)
{
	struct node *n, find, *res;

	find.ip = ip;
	res = RB_FIND(arp_tree, &arp_table->tree, &find);
	if (res) {
		free(res->ip);
		free(res->mac);
		n = res;
	} else {
		n = malloc(sizeof(struct node));
		if (!n)
			err(1, "malloc");
	}

	n->ip = strdup(ip);
	if (!n->ip)
		err(1, "strdup");

	n->mac = strdup(mac);
	if (!n->mac)
		err(1, "strdup");

	if (!res)
		RB_INSERT(arp_tree, &arp_table->tree, n);
}

char *
arp_table_find_by_ip(struct arp_table *arp_table, char *ip)
{
	struct node find, *res;

	find.ip = ip;
	res = RB_FIND(arp_tree, &arp_table->tree, &find);
	if (res)
		return res->mac;
	return NULL;
}

