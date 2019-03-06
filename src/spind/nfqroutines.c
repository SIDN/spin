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
#include "mainloop.h"
#include "nfqroutines.h"
#include "statistics.h"

STAT_MODULE(nfq)

#define NFQPERIOD   1000

static int
fd_set_blocking(int fd, int blocking) {
    /* Save the current flags */
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return 0;
    }

    if (blocking) {
        flags &= ~O_NONBLOCK;
    } else {
        flags |= O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) != -1;
}

#ifdef notdef
static void
processPacketData (uint8_t* data, int ret) {
    int i;

    for (i=0; i<ret;i++) {
        if (i%8 == 0) {
            printf("\n");
        }
        printf("%02x ", data[i]&0xFF);
    }
}
#endif

#ifdef notdef
static u_int32_t print_pkt (struct nfq_data *tb)
{
    int id = 0;
    struct nfqnl_msg_packet_hdr *ph;
    struct nfqnl_msg_packet_hw *hwph;
    u_int32_t mark,ifi;
    int ret;
    uint8_t* data;

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
        for (i = 0; i < hlen-1; i++) {
            printf("%02x:", hwph->hw_addr[i]);
        }
        printf("%02x ", hwph->hw_addr[hlen-1]);
    }

    mark = nfq_get_nfmark(tb);
    if (mark) {
        printf("mark=%u ", mark);
    }

    ifi = nfq_get_indev(tb);
    if (ifi) {
        printf("indev=%u ", ifi);
    }

    ifi = nfq_get_outdev(tb);
    if (ifi) {
        printf("outdev=%u ", ifi);
    }
    ifi = nfq_get_physindev(tb);
    if (ifi) {
        printf("physindev=%u ", ifi);
    }

    ifi = nfq_get_physoutdev(tb);
    if (ifi) {
        printf("physoutdev=%u ", ifi);
    }

    ret = nfq_get_payload(tb, &data);
    if (ret >= 0) {
        printf("payload_len=%d ", ret);
        processPacketData (data, ret);
    }
    fputc('\n', stdout);

    return id;
}
#endif

#define MAXNFR 5        /* More than this would be excessive */
static
struct nfreg {
    char *              nfr_name;       /* Name of module for debugging */
    nfqrfunc            nfr_wf;         /* The to-be-called work function */
    void *              nfr_wfarg;      /* Call back argument */
    int                 nfr_queue;      /* Queue number */
    struct nfq_q_handle *nfr_qh;        /* Queue handle */
    int                 nfr_packets;    /* Number of packets handled */
} nfr[MAXNFR];
static int n_nfr = 0;

static int
nfr_find_qh(struct nfq_q_handle *qh) {
    int i;

    for (i=0; i<n_nfr; i++) {
        if (nfr[i].nfr_qh == qh) {
            return i;
        }
    }
    return -1;
}

static int
nfq_cb_tcp(int fr_n, uint8_t* payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct tcphdr *tcp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    tcp_header = (struct tcphdr *) payload;
    src_port = ntohs(tcp_header->source);
    dest_port = ntohs(tcp_header->dest);

    hdrsize = 4*tcp_header->doff;
    return (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 6,
                payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static int
nfq_cb_udp(int fr_n, uint8_t* payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct udphdr *udp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    udp_header = (struct udphdr *) payload;
    src_port = ntohs(udp_header->source);
    dest_port = ntohs(udp_header->dest);

    hdrsize = 8;
    return (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 17,
                payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static int
nfq_cb_rest(int fr_n, uint8_t *payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {

    return (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 0,
                payload, payloadsize, s, d, 0, 0);
}

static int
nfq_cb_ipv4(int fr_n, uint8_t* payload, int payloadsize) {
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
    return nfq_cb_rest(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
}

static int
nfq_cb_ipv6(int fr_n, uint8_t* payload, int payloadsize) {
    struct ipv6hdr *ipv6_header;
    uint8_t src_addr[16], dest_addr[16];
    int hdrsize;

    ipv6_header = (struct ipv6hdr *) payload;

    memcpy(src_addr, &ipv6_header->saddr, 16);
    memcpy(dest_addr, &ipv6_header->daddr, 16);

    // handle options etc TODO
    hdrsize = 40;

    switch(ipv6_header->nexthdr) {
    case 6:
        // tcp
        return nfq_cb_tcp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
    case 17:
        // udp
        return nfq_cb_udp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
    }
    return 1;   // let's pass anyhow
}


static int
nfq_cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg, struct nfq_data *nfa, void *data)
{
        // u_int32_t id = print_pkt(nfa);
        u_int32_t id;
        int proto;
        uint8_t* payload;
        int payloadsize;
        struct nfqnl_msg_packet_hdr *ph;
        int fr_n;
        int verdict;
        STAT_COUNTER(ctr4, handled-ipv4, STAT_TOTAL);
        STAT_COUNTER(ctr6, handled-ipv6, STAT_TOTAL);
        STAT_COUNTER(ctrv, verdict, STAT_TOTAL);

        // printf("entering callback\n");
        ph = nfq_get_msg_packet_hdr(nfa);
        id = ntohl(ph->packet_id);
        proto = ntohs(ph->hw_protocol);
        payloadsize = nfq_get_payload(nfa, &payload);
        fr_n = nfr_find_qh(qh);
        switch (proto) {
        case 0x800:
            STAT_VALUE(ctr4, 1);
            verdict = nfq_cb_ipv4(fr_n, payload, payloadsize);
            break;
        case 0x86DD:
            STAT_VALUE(ctr6, 1);
            verdict = nfq_cb_ipv6(fr_n, payload, payloadsize);
            break;
        default:
            spin_log(LOG_DEBUG, "Unknown protocol %x\n", proto);
            // Who knows? Let's pass it on just in case
            verdict = 1;
        }
        nfr[fr_n].nfr_packets++;
        if (NFQPERIOD != 0 && nfr[fr_n].nfr_packets % NFQPERIOD == 0) {
            spin_log(LOG_INFO, "Nfq module %s handled %d packets\n", 
                nfr[fr_n].nfr_name,
                nfr[fr_n].nfr_packets);
        }
        // TODO what is verdict here
        STAT_VALUE(ctr4, verdict);
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

    spin_log(LOG_DEBUG, "Nfqroutine registered %s(..., %d)\n", name, queue);
    assert (n_nfr < MAXNFR) ;

    /*
     * At first call open library and call mainloop_register
     */
    if (n_nfr == 0) {
        spin_log(LOG_DEBUG, "opening library handle\n");
        library_handle = nfq_open();
        if (!library_handle) {
            spin_log(LOG_ERR, "error during nfq_open()\n");
            exit(1);
        }
        library_fd = nfq_fd(library_handle);
        fd_set_blocking(library_fd, 0);
        mainloop_register("nfq", wf_nfq, (void *) 0, library_fd, 0);
    }

    spin_log(LOG_DEBUG, "binding this socket to queue '%d'\n", queue);
    qh = nfq_create_queue(library_handle, queue, &nfq_cb, NULL);
    if (!qh) {
        spin_log(LOG_ERR, "error during nfq_create_queue()\n");
        exit(1);
    }

    spin_log(LOG_DEBUG, "setting copy_packet mode\n");
    if (nfq_set_mode(qh, NFQNL_COPY_PACKET, 0xffff) < 0) {
        spin_log(LOG_ERR, "can't set packet_copy mode\n");
        exit(1);
    }

    nfr[n_nfr].nfr_name = name;
    nfr[n_nfr].nfr_wf = wf;
    nfr[n_nfr].nfr_wfarg = arg;
    nfr[n_nfr].nfr_qh = qh;
    nfr[n_nfr].nfr_packets = 0;
    n_nfr++;
}
