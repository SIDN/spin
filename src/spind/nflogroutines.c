#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <linux/udp.h>
#include <linux/tcp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <libnetfilter_log/libnetfilter_log.h>
#include <fcntl.h>

#include <assert.h>

#include "mainloop.h"
#include "nflogroutines.h"
#include "spin_log.h"

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

#define MAXNFR 5        /* More than this would be excessive */
static
struct nfreg {
    char *              nfr_name;       /* Name of module for debugging */
    nflogfunc           nfr_wf;         /* The to-be-called work function */
    void *              nfr_wfarg;      /* Call back argument */
    int                 nfr_queue;      /* Queue number */
    struct nflog_g_handle *nfr_qh;        /* Queue handle */
    int                 nfr_packets;    /* Number of packets handled */
} nfr[MAXNFR];
static int n_nfr = 0;

static int
nfr_find_qh(struct nflog_g_handle *qh) {
    int i;

    for (i=0; i<n_nfr; i++) {
        if (nfr[i].nfr_qh == qh) {
            return i;
        }
    }
    return -1;
}

static void
nflog_cb_tcp(int fr_n, uint8_t* payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct tcphdr *tcp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    tcp_header = (struct tcphdr *) payload;
    src_port = ntohs(tcp_header->source);
    dest_port = ntohs(tcp_header->dest);

    hdrsize = 4*tcp_header->doff;
    (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 6,
        payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static void
nflog_cb_udp(int fr_n, uint8_t* payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {
    struct udphdr *udp_header;
    unsigned src_port, dest_port;
    int hdrsize;

    udp_header = (struct udphdr *) payload;
    src_port = ntohs(udp_header->source);
    dest_port = ntohs(udp_header->dest);

    hdrsize = 8;
    (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 17,
         payload+hdrsize, payloadsize-hdrsize, s, d, src_port, dest_port);
}

static void
nflog_cb_rest(int fr_n, uint8_t *payload, int payloadsize, int af, uint8_t *s, uint8_t *d) {

    (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, af, 0,
         payload, payloadsize, s, d, 0, 0);
}

static void
nflog_cb_ipv4(int fr_n, uint8_t* payload, int payloadsize) {
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
        nflog_cb_tcp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
        break;
    case 17:
        // udp
        nflog_cb_udp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
        break;
    }
    nflog_cb_rest(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET, src_addr, dest_addr);
}

static void
nflog_cb_ipv6(int fr_n, uint8_t* payload, int payloadsize) {
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
        nflog_cb_tcp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
        break;
    case 17:
        // udp
        nflog_cb_udp(fr_n, payload + hdrsize, payloadsize - hdrsize, AF_INET6, src_addr, dest_addr);
        break;
    }
}


static int
nflog_cb(struct nflog_g_handle *qh, struct nfgenmsg *nfmsg, struct nflog_data *nfa, void *data)
{
        // u_int32_t id = print_pkt(nfa);
        int proto;
        uint8_t* payload;
        int payloadsize;
        struct nfulnl_msg_packet_hdr *ph;
        int fr_n;

        // printf("entering callback\n");
        ph = nflog_get_msg_packet_hdr(nfa);
        proto = ntohs(ph->hw_protocol);
        payloadsize = nflog_get_payload(nfa, (char**)&payload);
        fr_n = nfr_find_qh(qh);
        switch (proto) {
        case 0x800:
            nflog_cb_ipv4(fr_n, payload, payloadsize);
            break;
        case 0x86DD:
            nflog_cb_ipv6(fr_n, payload, payloadsize);
            break;
        default:
            spin_log(LOG_DEBUG, "Unknown protocol %x\n", proto);
        }
        nfr[fr_n].nfr_packets++;
        if (nfr[fr_n].nfr_packets % 1000 == 0) {
            spin_log(LOG_INFO, "nflog module %s handled %d packets\n", 
                nfr[fr_n].nfr_name,
                nfr[fr_n].nfr_packets);
        }
        return 0;
}

static struct nflog_handle *library_handle = NULL;
static int library_fd;

static void
wf_nfq(void *arg, int data, int timeout) {
    char buf[4096] __attribute__ ((aligned));
    int rv;

    if (data) {
        while ((rv = recv(library_fd, buf, sizeof(buf), 0)) > 0)
        {
            nflog_handle_packet(library_handle, buf, rv);
        }
    }
    if (timeout) {
        // nothing
    }
}


// Register work function:  timeout in millisec
void nflogroutine_register(char *name, nflogfunc wf, void *arg, int group_number) {
    struct nflog_g_handle *qh;

    spin_log(LOG_DEBUG, "nflogroutine registered %s(..., %d)\n", name, group_number);
    assert (n_nfr < MAXNFR) ;

    /*
     * At first call open library and call mainloop_register
     */
    if (n_nfr == 0) {
        spin_log(LOG_DEBUG, "opening library handle\n");
        library_handle = nflog_open();
        if (!library_handle) {
            spin_log(LOG_ERR, "error during nflog_open()\n");
            exit(1);
        }
        library_fd = nflog_fd(library_handle);
        fd_set_blocking(library_fd, 0);
        mainloop_register("nfq", wf_nfq, (void *) 0, library_fd, 0);
    }

    spin_log(LOG_DEBUG, "binding this socket to group '%d'\n", group_number);
    qh = nflog_bind_group(library_handle, group_number);
    if (!qh) {
        spin_log(LOG_ERR, "error during nflog_bind_group()\n");
        exit(1);
    }

    nflog_callback_register(qh, &nflog_cb, NULL);

    spin_log(LOG_DEBUG, "setting copy_packet mode\n");
    if (nflog_set_mode(qh, NFULNL_COPY_PACKET, 0xffff) < 0) {
        spin_log(LOG_ERR, "can't set packet_copy mode\n");
        exit(1);
    }

    nfr[n_nfr].nfr_name = name;
    nfr[n_nfr].nfr_wf = wf;
    nfr[n_nfr].nfr_wfarg = arg;
    nfr[n_nfr].nfr_qh = qh;
    n_nfr++;
}

void nflogroutine_close(char* name) {
    int i;
    for (i=0; i < n_nfr; i++) {
        if (strcmp(nfr[i].nfr_name, name) == 0) {
            nflog_unbind_group(nfr[i].nfr_qh);
        }
    }
}

void nflog_close_handle() {
    if (library_handle != NULL) {
        nflog_close(library_handle);
    }
}
