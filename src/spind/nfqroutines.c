#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>		
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <libnetfilter_queue/libnetfilter_queue.h>
#include <fcntl.h>

#include <assert.h>

#include "spin_log.h"
//#include "pkt_info.h"
#include "nfqroutines.h"

static int
fd_set_blocking(int fd, int blocking) {
    /* Save the current flags */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
	return 0;

    if (blocking)
	flags &= ~O_NONBLOCK;
    else
	flags |= O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) != -1;
}

static void
processPacketData (char *data, int ret) {
    int i;

    for (i=0; i<ret;i++) {
	if (i%8 == 0)
	    printf("\n");
	printf("%02x ", data[i]&0xFF);
    }
}

static u_int32_t print_pkt (struct nfq_data *tb)
{
	int id = 0;
	struct nfqnl_msg_packet_hdr *ph;
	struct nfqnl_msg_packet_hw *hwph;
	u_int32_t mark,ifi; 
	int ret;
	char *data;

	ph = nfq_get_msg_packet_hdr(tb);
	if (ph) {
		id = ntohl(ph->packet_id);
		printf("hw_protocol=0x%04x hook=%u id=%u ",
			ntohs(ph->hw_protocol), ph->hook, id);
	}

	hwph = nfq_get_packet_hw(tb);
	if (hwph) {
		int i, hlen = ntohs(hwph->hw_addrlen);

		printf("hw_src_addr=");
		for (i = 0; i < hlen-1; i++)
			printf("%02x:", hwph->hw_addr[i]);
		printf("%02x ", hwph->hw_addr[hlen-1]);
	}

	mark = nfq_get_nfmark(tb);
	if (mark)
		printf("mark=%u ", mark);

	ifi = nfq_get_indev(tb);
	if (ifi)
		printf("indev=%u ", ifi);

	ifi = nfq_get_outdev(tb);
	if (ifi)
		printf("outdev=%u ", ifi);
	ifi = nfq_get_physindev(tb);
	if (ifi)
		printf("physindev=%u ", ifi);

	ifi = nfq_get_physoutdev(tb);
	if (ifi)
		printf("physoutdev=%u ", ifi);

	ret = nfq_get_payload(tb, &data);
	if (ret >= 0) {
		printf("payload_len=%d ", ret);
		processPacketData (data, ret);
	}
	fputc('\n', stdout);

	return id;
}
	
#define UDP_HEADER_SIZE 8
#define TCP_HEADER_SIZE 20

#define IPV6_HEADER_SIZE 40

#ifdef notdef
int parse_ipv6_packet(char *data, nt len, pkt_info_t* pkt_info) {
    //uint8_t* b;
    struct udphdr *udp_header;
    struct tcphdr *tcp_header;
    struct ipv6hdr *ipv6_header;

    ipv6_header = (struct ipv6hdr *) data;

    // hard-code a number of things to ignore; such as broadcasts (for now)
    /*
    b = (uint8_t*)(&ipv6_header->daddr);
    if (*b == 255) {
        return -1;
    }*/

    if (ipv6_header->nexthdr == 17) {
        udp_header = (struct udphdr *) data+IPV6_HEADER_SIZE;
        pkt_info->src_port = ntohs(udp_header->source);
        pkt_info->dest_port = ntohs(udp_header->dest);
        pkt_info->payload_size = (uint32_t)ntohs(udp_header->len) - UDP_HEADER_SIZE;
        pkt_info->payload_offset = data+IPV6_HEADER_SIZE + UDP_HEADER_SIZE;
    } else if (ipv6_header->nexthdr == 6) {
        tcp_header = (struct tcphdr *) data+IPV6_HEADER_SIZE;
        pkt_info->src_port = ntohs(tcp_header->source);
        pkt_info->dest_port = ntohs(tcp_header->dest);
        pkt_info->payload_size = (uint32_t)len - TCP_HEADER_SIZE - (4*tcp_header->doff);
        if (pkt_info->payload_size > 2) {
            pkt_info->payload_size = pkt_info->payload_size - 2;
            pkt_info->payload_offset = data+IPV6_HEADER_SIZE + TCP_HEADER_SIZE + (4*tcp_header->doff) + 2;
        } else {
            // if size is zero, ignore tcp packet
            printf(stderr, "Zero length TCP packet, ignoring\n");
            pkt_info->payload_size = 0;
            //pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
            pkt_info->payload_offset = 0;
        }
    } else if (ipv6_header->nexthdr != 58) {
        if (ipv6_header->nexthdr == 0) {
            // ignore hop-by-hop option header
            return 1;
        } else if (ipv6_header->nexthdr == 44) {
            // what to do with fragments?
            return 1;
        }
        printf(stderr, "unsupported IPv6 next header: %u\n", ipv6_header->nexthdr);
        return -1;
    } else {
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff);
        pkt_info->payload_offset = skb_network_header_len(sockbuff);
    }

    // rest of basic info
    pkt_info->packet_count = 1;
    pkt_info->family = AF_INET6;
    pkt_info->protocol = ipv6_header->nexthdr;
    memcpy(pkt_info->src_addr, &ipv6_header->saddr, 16);
    memcpy(pkt_info->dest_addr, &ipv6_header->daddr, 16);
    return 0;
}

#define IPV4_HEADER_SIZE 20

// Parse packet into pkt_info structure
// Return values:
// 0: all ok, structure filled
// 1: zero-size packet, structure not filled
// -1: error
int parse_packet(char *data, len, pkt_info_t* pkt_info) {
    //uint8_t* b;
    struct udphdr *udp_header;
    struct tcphdr *tcp_header;
    struct ipvhdr *ip_header;

    ip_header = (struct iphdr *)skb_network_header(sockbuff);
    if (ip_header->version == 6) {
        return parse_ipv6_packet(sockbuff, pkt_info);
    }

    // hard-code a number of things to ignore; such as broadcasts (for now)
    // TODO: this should be based on netmask...
    /*
    b = (uint8_t*)(&ip_header->daddr);
    if (*b == 255 ||
        *b == 224 ||
        *b == 239) {
        return -1;
    }*/

    if (ip_header->protocol == 17) {
        udp_header = (struct udphdr *)skb_transport_header(sockbuff);
        pkt_info->src_port = ntohs(udp_header->source);
        pkt_info->dest_port = ntohs(udp_header->dest);
        pkt_info->payload_size = (uint32_t)ntohs(udp_header->len) - 8;
        pkt_info->payload_offset = skb_network_header_len(sockbuff) + 8;
    } else if (ip_header->protocol == 6) {
        tcp_header = (struct tcphdr*)((char*)ip_header + (ip_header->ihl * 4));
        pkt_info->src_port = ntohs(tcp_header->source);
        pkt_info->dest_port = ntohs(tcp_header->dest);
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff) - (4*tcp_header->doff);
        if (pkt_info->payload_size > 2) {
            pkt_info->payload_size = pkt_info->payload_size - 2;
            //pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
            pkt_info->payload_offset = 0;
        } else {
            // if size is zero, ignore tcp packet
            printf(stderr "Payload size: %u\n", pkt_info->payload_size);
            pkt_info->payload_size = 0;
            pkt_info->payload_offset = skb_network_header_len(sockbuff) + (4*tcp_header->doff) + 2;
        }
    /* ignore some protocols */
    // TODO: de-capsulate encapsulated ipv6?
    } else if (ip_header->protocol != 1 &&
               ip_header->protocol != 2 &&
               ip_header->protocol != 41
              ) {
        printf(stderr "unsupported IPv4 protocol: %u\n", ip_header->protocol);
        return -1;
    } else {
        pkt_info->payload_size = (uint32_t)sockbuff->len - skb_network_header_len(sockbuff);
        pkt_info->payload_offset = skb_network_header_len(sockbuff);
    }

    // rest of basic info
    pkt_info->packet_count = 1;
    pkt_info->family = AF_INET;
    pkt_info->protocol = ip_header->protocol;
    memset(pkt_info->src_addr, 0, 12);
    memcpy(pkt_info->src_addr + 12, &ip_header->saddr, 4);
    memset(pkt_info->dest_addr, 0, 12);
    memcpy(pkt_info->dest_addr + 12, &ip_header->daddr, 4);
    return 0;
}
#endif

#define MAXNFR 5	/* More than this would be excessive */
static
struct nfreg {
    char *		nfr_name;	/* Name of module for debugging */
    nfqrfunc		nfr_wf;		/* The to-be-called work function */
    void *		nfr_wfarg;	/* Call back argument */
    int			nfr_queue;	/* Queue number */
    struct nfq_q_handle *nfr_qh;	/* Queue handle */
} nfr[MAXNFR];
static int n_nfr = 0;

static int
nfr_find_qh(struct nfq_q_handle *qh) {
    int i;

    for (i=0; i<n_nfr; i++) {
	if (nfr[i].nfr_qh == qh)
	    return i;
    }
    return -1;
}

static int
nfr_mapproto(int p) {

    if (p == 0x800)
	return 4;
    if (p == 0x86DD)
	return 6;
    return 0;
}

static int
nfq_cb_tcp(int fr_n, char *payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct tcphdr *tcp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    tcp_header = (struct tcphdr *) payload;
    src_port = ntohs(tcp_header->source);
    dest_port = ntohs(tcp_header->dest);

    hdrsize = 4*tcp_header->doff;
    return (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af,
    		payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static int
nfq_cb_udp(int fr_n, char *payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct udphdr *udp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    udp_header = (struct udphdr *) payload;
    src_port = ntohs(udp_header->source);
    dest_port = ntohs(udp_header->dest);

    hdrsize = 8;
    return (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af,
    		payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static int
nfq_cb_ipv4(int fr_n, char *payload, int payloadsize) {
    struct iphdr *ip_header;
    uint8_t src_addr[16], dest_addr[16];
    int hdrsize;

    ip_header = (struct iphdr *) payload;

    memset(src_addr, 0, 12);
    memcpy(src_addr + 12, &ip_header->saddr, 4);
    memset(dest_addr, 0, 12);
    memcpy(dest_addr + 12, &ip_header->daddr, 4);

    // handle options etc TODO
    hdrsize = ip_header->ihl * 4;

    switch(ip_header->protocol) {
    case 6:
	// tcp
	return nfq_cb_tcp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
    case 17:
	// udp
	return nfq_cb_udp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
    }
    return 1;	// let's pass anyhow
}

static int
nfq_cb_ipv6(int fr_n, char *payload, int payloadsize) {
    struct ipv6hdr *ipv6_header;
    uint8_t src_addr[16], dest_addr[16];
    int hdrsize;

    ipv6_header = (struct ipv6hdr *) payload;

    memcpy(src_addr, &ipv6_header->saddr, 16);
    memcpy(dest_addr, &ipv6_header->daddr, 16);

    // handle options etc TODO
    hdrsize = IPV6_HEADER_SIZE;

    switch(ipv6_header->nexthdr) {
    case 6:
	// tcp
	return nfq_cb_tcp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
    case 17:
	// udp
	return nfq_cb_udp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
    }
    return 1;	// let's pass anyhow
}


static int
nfq_cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg, struct nfq_data *nfa, void *data)
{
	u_int32_t id = print_pkt(nfa);
	//u_int32_t id;
	int proto;
	char *payload;
	int payloadsize;
        struct nfqnl_msg_packet_hdr *ph;
	int fr_n;
	int verdict;

	printf("entering callback\n");
	ph = nfq_get_msg_packet_hdr(nfa);	
	id = ntohl(ph->packet_id);
	proto = ntohs(ph->hw_protocol);
	payloadsize = nfq_get_payload(nfa, &payload);
	fr_n = nfr_find_qh(qh);
	switch (nfr_mapproto(proto)) {
	case 4:
	    verdict = nfq_cb_ipv4(fr_n, payload, payloadsize);
	    break;
	case 6:
	    verdict = nfq_cb_ipv6(fr_n, payload, payloadsize);
	    break;
	default:
	    // Who knows? Let's pass it on just in case
	    verdict = 1;
	}
	// TODO what is verdict here
	return nfq_set_verdict(qh, id, verdict ? NF_ACCEPT : NF_DROP, 0, NULL);
}

static struct nfq_handle *library_handle;
static int library_fd;

static void
wf_nfq(void *arg, int data, int timeout) {
    char buf[4096] __attribute__ ((aligned));
    int rv;

    if (data) {
	while ((rv = recv(library_fd, buf, sizeof(buf), 0)) > 0)
	{
	    printf("pkt received\n");
	    nfq_handle_packet(library_handle, buf, rv);
	}
    }
    if (timeout) {
	// nothing
    }
}


// Register work function:  timeout in millisec
void nfqroutine_register(char *name, nfqrfunc wf, void *arg, int queue) {
    struct nfq_q_handle *qh;
    int i;

    spin_log(LOG_DEBUG, "Nfqroutine registered %s(..., %d)\n", name, queue);
    assert (n_nfr < MAXNFR) ;

    /*
     * At first call open library and call mainloop_register
     */
    if (n_nfr == 0) {
	printf("opening library handle\n");
	library_handle = nfq_open();
	if (!library_handle) {
	    fprintf(stderr, "error during nfq_open()\n");
	    exit(1);
	}
	library_fd = nfq_fd(library_handle);
	fd_set_blocking(library_fd, 0);
	mainloop_register("nfq", wf_nfq, (void *) 0, library_fd, 0);
    }

    printf("binding this socket to queue '%d'\n", queue);
    qh = nfq_create_queue(library_handle, queue, &nfq_cb, NULL);
    if (!qh) {
	fprintf(stderr, "error during nfq_create_queue()\n");
	exit(1);
    }

    printf("setting copy_packet mode\n");
    if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
	fprintf(stderr, "can't set packet_copy mode\n");
	exit(1);
    }

    nfr[n_nfr].nfr_name = name;
    nfr[n_nfr].nfr_wf = wf;
    nfr[n_nfr].nfr_wfarg = arg;
    nfr[n_nfr].nfr_qh = qh;
    n_nfr++;
}

#ifdef notdef

static int cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg, struct nfq_data *nfa, void *data)
{
	u_int32_t id = print_pkt(nfa);
	//u_int32_t id;

        struct nfqnl_msg_packet_hdr *ph;
	ph = nfq_get_msg_packet_hdr(nfa);	
	id = ntohl(ph->packet_id);
	printf("entering callback\n");
	return nfq_set_verdict(qh, id, NF_ACCEPT, 0, NULL);
}

int main(int argc, char **argv)
{
	struct nfq_handle *h;
	struct nfq_q_handle *qh;
	int fd;
	int rv;
	char buf[4096] __attribute__ ((aligned));

	printf("opening library handle\n");
	h = nfq_open();
	if (!h) {
		fprintf(stderr, "error during nfq_open()\n");
		exit(1);
	}

	printf("unbinding existing nf_queue handler for AF_INET (if any)\n");
	if (nfq_unbind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_unbind_pf()\n");
#ifdef notdef
		exit(1);
#endif
	}

	printf("binding nfnetlink_queue as nf_queue handler for AF_INET\n");
	if (nfq_bind_pf(h, AF_INET) < 0) {
		fprintf(stderr, "error during nfq_bind_pf()\n");
#ifdef notdef
		exit(1);
#endif
	}

	printf("binding this socket to queue '0'\n");
	qh = nfq_create_queue(h,  0, &cb, NULL);
	if (!qh) {
		fprintf(stderr, "error during nfq_create_queue()\n");
		exit(1);
	}

	printf("setting copy_packet mode\n");
	if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
		fprintf(stderr, "can't set packet_copy mode\n");
		exit(1);
	}

	fd = nfq_fd(h);

	// para el tema del loss:   while ((rv = recv(fd, buf, sizeof(buf), 0)) && rv >= 0)

	while ((rv = recv(fd, buf, sizeof(buf), 0)))
	{
		printf("pkt received\n");
		nfq_handle_packet(h, buf, rv);
	}

	printf("unbinding from queue 0\n");
	nfq_destroy_queue(qh);

#ifdef INSANE
	/* normally, applications SHOULD NOT issue this command, since
	 * it detaches other programs/sockets from AF_INET, too ! */
	printf("unbinding from AF_INET\n");
	nfq_unbind_pf(h, AF_INET);
#endif

	printf("closing library handle\n");
	nfq_close(h);

	exit(0);
}
#endif
