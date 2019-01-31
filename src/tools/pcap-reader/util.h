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

#include <stddef.h>

#if DEBUG
#define DPRINTF(x...) warnx(x)
#define DASSERT(x) assert(x)
#else
#define DPRINTF(x...) do {} while (0)
#define DASSERT(x) do {} while (0)
#endif /* DEBUG */

/* JSON output helpers */
void print_str_str(const char *a, const char *b);
void print_str_n(const char *a, long long d);
void print_fromto(const char *header, const char *mac, const char *ip);
void print_dummy_end(void);

struct ether_addr;
int mactostr(struct ether_addr *ea, char *s, size_t len);

