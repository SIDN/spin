/*
 * Copyright (c) 2017-2018 Caspar Schutijser <caspar.schutijser@sidn.nl>
 *
 * Portions of this code were taken from tcpdump, which includes this notice:
 *
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <sys/types.h>

#include <net/if.h>
#ifdef HAVE_NET_ETHERTYPES_H
#include <net/ethertypes.h>
#endif // HAVE_NET_ETHERTYPES_H
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet/if_ether.h>
#define __FAVOR_BSD /* Who doesn't? */
#include <netinet/tcp.h>
#include <netinet/udp.h>

#include <assert.h>
#include <endian.h>
#include <err.h>
#include <errno.h>
#include <pcap.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "external/external.h"
#include "external/interface.h"
#include "external/extract.h" /* must come after interface.h */

#include "nodes.h"
#include "sleep.h"
#include "socket.h"

/* spind/lib includes */
#include "dns.h"
#include "extsrc.h"
#include "pkt_info.h"
#include "spinhook.h"

/* Linux compat */
#ifndef IPV6_VERSION
#define IPV6_VERSION		0x60
#define IPV6_VERSION_MASK	0xf0
#endif /* IPV6_VERSION */

#if DEBUG
#define DPRINTF(x...) warnx(x)
#define DASSERT(x) assert(x)
#else
#define DPRINTF(x...) do {} while (0)
#define DASSERT(x) do {} while (0)
#endif /* DEBUG */

#ifdef __OpenBSD__
extern char *malloc_options;
#endif /* __OpenBSD__ */

int Rflag;	/* replay as fast as possible rather than at recorded speed */

const u_char *packetp;
const u_char *snapend;

static pcap_t *pd;

static struct handle_dns_ctx *handle_dns_ctx;

static int fd;

static node_cache_t *node_cache;

static void
sig_handler(int sig)
{
	int save_errno = errno;

	switch (sig) {
	case SIGINT:
	case SIGTERM:
		pcap_breakloop(pd);
		break;

	default:
		break;
	}

	errno = save_errno;
}

static void
usage(const char *error)
{
	extern char *__progname;

	if (error)
		fprintf(stderr, "%s\n", error);
	fprintf(stderr, "Usage: %s [-R] [-e extsrc-socket-path] [-f filter]\n",
	    __progname);
	fprintf(stderr, "\t[-i interface] [-m mac] [-r file]\n");
	exit(1);
}

static void
write_pkt_info_to_socket(pkt_info_t *pkt)
{
	struct extsrc_msg *msg;

	msg = extsrc_msg_create_pkt_info(pkt);

	socket_writemsg(fd, msg->data, msg->length);

	extsrc_msg_free(msg);
}

static void
dns_query_hook(dns_pkt_info_t *dns_pkt, int family, uint8_t *src_addr)
{
	struct extsrc_msg *msg;

	msg = extsrc_msg_create_dns_query(dns_pkt, family, src_addr);

	socket_writemsg(fd, msg->data, msg->length);

	extsrc_msg_free(msg);
}

static void
dns_answer_hook(dns_pkt_info_t *dns_pkt)
{
	struct extsrc_msg *msg;

	msg = extsrc_msg_create_dns_answer(dns_pkt);

	socket_writemsg(fd, msg->data, msg->length);

	extsrc_msg_free(msg);
}

static void
handle_icmp6(const u_char *bp, const struct ether_header *ep)
{
	const struct icmp6_hdr *dp;
	const struct nd_neighbor_advert *p;

	dp = (struct icmp6_hdr *)bp;

	TCHECK(dp->icmp6_code);
	switch (dp->icmp6_type) {
	case ND_NEIGHBOR_ADVERT:
		p = (const struct nd_neighbor_advert *)dp;

		TCHECK(p->nd_na_target);

		x_node_cache_add_mac_uint8t(node_cache, ep->ether_shost);
		mark_local_device(fd, node_cache, ep->ether_shost,
		    p->nd_na_target.s6_addr, AF_INET6);
		break;

	default:
		break;
	}

	return;

 trunc:
	warnx("TRUNCATED");
}

static void
handle_dns(const u_char *cp, u_int len, int family, uint8_t *src_addr,
    u_int src_port, u_int dest_port)
{
	if (src_port == 53) {
		handle_dns_answer(handle_dns_ctx, cp, len, family);
	} else if (dest_port == 53) {
		handle_dns_query(handle_dns_ctx, cp, len, src_addr, family);
	}
}

static void
handle_l4(const struct ether_header *ep, const u_char *l4, u_int len,
    pkt_info_t *pkt_info, int truncated)
{
	const struct tcphdr *tp;
	const struct udphdr *up;
	const u_char *cp;
	u_int cplen;
#ifdef unusedfornow
	int tcp_initiated = 0;
#endif

	switch (pkt_info->protocol) {
	case IPPROTO_ICMPV6:
		if (pkt_info->family == AF_INET6)
			handle_icmp6(l4, ep);
		break;

	case IPPROTO_TCP:
		tp = (const struct tcphdr *)l4;
		TCHECK(*tp);

#ifdef unusedfornow
		if ((tp->th_flags & (TH_SYN|TH_ACK)) == TH_SYN)
			tcp_initiated = 1;
#endif

		pkt_info->src_port = ntohs(tp->th_sport);
		pkt_info->dest_port = ntohs(tp->th_dport);
		break;

	case IPPROTO_UDP:
		up = (const struct udphdr *)l4;
		TCHECK(*up);

		pkt_info->src_port = ntohs(up->uh_sport);
		pkt_info->dest_port = ntohs(up->uh_dport);
		break;

	default:
		DPRINTF("Unknown protocol: %d", pkt_info->protocol);
		break;
	}

	write_pkt_info_to_socket(pkt_info);

	if (pkt_info->src_port == 53 || pkt_info->dest_port == 53) {
		if (truncated) {
			warnx("not attempting to parse DNS packet");
		} else {
			if (up) {
				cp = (const u_char *)(up + 1);
				cplen = len - sizeof(struct udphdr);
			} else {
				cp = (const u_char *)(tp + 1);
				cplen = len - sizeof(struct tcphdr);
			}
			TCHECK(*cp);
			handle_dns(cp, cplen, pkt_info->family,
			    pkt_info->src_addr, pkt_info->src_port,
			    pkt_info->dest_port);
		}
	}

	return;

trunc:
	warnx("TRUNCATED");
}

static void
#ifdef __OpenBSD__ /* XXX */
handle_ip(const u_char *p, u_int caplen, const struct ether_header *ep,
    const struct bpf_timeval *ts)
#else
handle_ip(const u_char *p, u_int caplen, const struct ether_header *ep,
    const struct timeval *ts)
#endif
{
	const struct ip *ip;
	u_int hlen, len;
	pkt_info_t pkt_info;
	int truncated = 0;

	if (((struct ip *)p)->ip_v != 4) {
		DPRINTF("not an IP packet");
		return;
	}

	ip = (struct ip *)p;

	memset(&pkt_info, 0, sizeof(pkt_info_t));

	pkt_info.family = AF_INET;

	TCHECK(*ip);
	len = ntohs(ip->ip_len);
	if (caplen < len) {
		warnx("Truncated IP packet: %d bytes missing", len - caplen);
		len = caplen; // XXX
		truncated = 1;
	}
	hlen = ip->ip_hl * 4;
	if (hlen < sizeof(struct ip) || hlen > len) {
		warnx("Bad header length: %d", hlen);
		return;
	}
	len -= hlen;

	pkt_info.protocol = ip->ip_p;
	memset(pkt_info.src_addr, 0, 12);
	memcpy(pkt_info.src_addr + 12, &ip->ip_src, sizeof(ip->ip_src));
	memset(pkt_info.dest_addr, 0, 12);
	memcpy(pkt_info.dest_addr + 12, &ip->ip_dst, sizeof(ip->ip_dst));

	pkt_info.payload_size = len;
	pkt_info.packet_count = 1;

	handle_l4(ep, (const u_char *)ip + hlen, len, &pkt_info, truncated);

	return;

 trunc:
	warnx("TRUNCATED");
}

static void
#ifdef __OpenBSD__ /* XXX */
handle_ip6(const u_char *p, u_int caplen, const struct ether_header *ep,
    const struct bpf_timeval *ts)
#else
handle_ip6(const u_char *p, u_int caplen, const struct ether_header *ep,
    const struct timeval *ts)
#endif
{
	const struct ip6_hdr *ip6;
	u_int hlen, len;
	pkt_info_t pkt_info;
	int truncated = 0;

	if (((struct ip *)p)->ip_v != 6) {
		DPRINTF("not an IP packet");
		return;
	}

	ip6 = (struct ip6_hdr *)p;

	memset(&pkt_info, 0, sizeof(pkt_info_t));

	pkt_info.family = AF_INET6;

	if (caplen < sizeof(struct ip6_hdr)) {
		warnx("Truncated IPv6 packet: %d", caplen);
		return;
	}
	if ((ip6->ip6_vfc & IPV6_VERSION_MASK) != IPV6_VERSION) {
		warnx("Bad IPv6 version: %u", ip6->ip6_vfc >> 4);
		return;
	}
	hlen = sizeof(struct ip6_hdr);

	len = ntohs(ip6->ip6_plen);
	if (caplen < len + hlen) {
		warnx("Truncated IP6 packet: %d bytes missing",
		    len + hlen - caplen);
		truncated = 1;
	}

	// XXX extension headers
	pkt_info.protocol = ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt;
	memcpy(pkt_info.src_addr, &ip6->ip6_src, sizeof(ip6->ip6_src));
	memcpy(pkt_info.dest_addr, &ip6->ip6_dst, sizeof(ip6->ip6_dst));

	pkt_info.payload_size = len;
	pkt_info.packet_count = 1;

	handle_l4(ep, (const u_char *)ip6 + hlen, len, &pkt_info, truncated);
}

static void
handle_arp(const u_char *bp, u_int length)
{
	const struct ether_arp *ap;
	u_short op;

	ap = (struct ether_arp *)bp;
	if ((u_char *)(ap + 1) > snapend) {
		warnx("[|arp]");
		return;
	}
	if (length < sizeof(struct ether_arp)) {
		warnx("truncated arp");
		return;
	}

	op = EXTRACT_16BITS(&ap->arp_op);
	switch (op) {
	case ARPOP_REPLY:
		x_node_cache_add_mac_uint8t(node_cache, ap->arp_sha);
		mark_local_device(fd, node_cache, ap->arp_sha, ap->arp_spa,
		    AF_INET);
		break;

	default:
		break;
	}
}

/*
 * Callback for libpcap. Handles a packet.
 */
static void
callback(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	const u_char *p;
	const struct ether_header *ep;
	u_short ether_type;
	u_int caplen = h->caplen;

	if (h->caplen != h->len) {
		warnx("caplen %d != len %d, ", h->caplen, h->len);
	}

	packetp = sp;
	snapend = sp + h->caplen;

	p = sp;

	if ((snapend - p) < sizeof(struct ether_header)) {
		warnx("[|ether]");
		return;
	}

	ep = (const struct ether_header *)p;
	ether_type = ntohs(ep->ether_type);

	p = sp + 14; /* Move past Ethernet header */
	caplen -= 14;

	// XXX find a better place for this piece of code
	if (!Rflag)
		maybe_sleep(&h->ts);

	switch (ether_type) {
	case ETHERTYPE_IP:
		handle_ip(p, caplen, ep, &h->ts);
		break;

	case ETHERTYPE_IPV6:
		handle_ip6(p, caplen, ep, &h->ts);
		break;

	case ETHERTYPE_ARP:
		handle_arp(p, caplen);
		break;

	default:
		DPRINTF("unknown ether type");
		break;
	}
}

int
main(int argc, char *argv[])
{
	int ch;
	char *extsrc_socket_path = EXTSRC_SOCKET_PATH;
	char *device = NULL;
	char *file = NULL;
	char *pcap_errbuf;
	char *filter = "";
	struct bpf_program fp;

#ifdef __OpenBSD__
	/* Configure malloc on OpenBSD; enables security auditing options */
	malloc_options = "S";
#endif /* __OpenBSD__ */

	node_cache = node_cache_create();

	while ((ch = getopt(argc, argv, "e:f:hi:m:Rr:")) != -1) {
		switch(ch) {
		case 'e':
			extsrc_socket_path = optarg;
			break;
		case 'f':
			filter = optarg;
			break;
		case 'h':
			usage(NULL);
		case 'i':
			device = optarg;
			break;
		case 'm':
			// XXX should do input validation
			x_node_cache_add_mac_macstr(node_cache, optarg);
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'r':
			file = optarg;
			break;
		default:
			usage(NULL);
		}
	}

	if (device && file)
		usage("cannot specify both an interface and a file");
	if (!device && !file)
		device = "eth0";

	fd = socket_open(extsrc_socket_path);

	if ((pcap_errbuf = malloc(PCAP_ERRBUF_SIZE)) == NULL)
		err(1, "malloc");

	if (device) {
		Rflag = 1;

		pd = pcap_create(device, pcap_errbuf);
		if (!pd)
			errx(1, "pcap_create: %s", pcap_errbuf);

		if (pcap_set_snaplen(pd, 1514) != 0)
			errx(1, "pcap_set_snaplen");

		if (pcap_set_promisc(pd, 1) != 0)
			errx(1, "pcap_set_promisc");

		if (pcap_set_timeout(pd, 1000) != 0)
			errx(1, "pcap_set_timeout");

		if (pcap_activate(pd) != 0)
			errx(1, "pcap_activate: %s", pcap_geterr(pd));
	} else {
#ifdef HAVE_PLEDGE
		if (pledge("stdio bpf rpath", NULL) == -1)
			err(1, "pledge");
#endif /* HAVE_PLEDGE */

		pd = pcap_open_offline(file, pcap_errbuf);
		if (!pd)
			errx(1, "pcap_open_offline: %s", pcap_errbuf);
	}

	if (pcap_datalink(pd) != DLT_EN10MB)
		errx(1, "the device is not an Ethernet device");

	if (pcap_compile(pd, &fp, filter, 1, 0) == -1 ||
	    pcap_setfilter(pd, &fp) == -1)
		errx(1, "could not set filter: %s", pcap_geterr(pd));

#ifdef HAVE_PLEDGE
	if (pledge("stdio bpf", NULL) == -1)
		err(1, "pledge");
#endif /* HAVE_PLEDGE */

	handle_dns_ctx = handle_dns_init(&dns_query_hook, &dns_answer_hook);
	if (!handle_dns_ctx)
		errx(1, "handle_dns_init");

	(void)signal(SIGTERM, sig_handler);
	(void)signal(SIGINT, sig_handler);

	if (pcap_loop(pd, -1, callback, NULL) == -1)
		errx(1, "pcap_loop: %s", pcap_geterr(pd));

	/*
	 * Done, clean up.
	 */
	if (pd)
		pcap_close(pd);

	close(fd);
	node_cache_destroy(node_cache);

	return 0;
}

