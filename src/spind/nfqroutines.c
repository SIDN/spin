#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <linux/types.h>
#include <linux/netfilter.h>		
#include <libnetfilter_queue/libnetfilter_queue.h>

#include <assert.h>

#include "spin_log.h"
#include "nfqroutines.h"

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
		// processPacketData (data, ret);
	}
	fputc('\n', stdout);

	return id;
}
	
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
	proto = ntohs(ph->hw_protocol);
	payloadsize = nfq_get_payload(nfa, &payload);
	fr_n = nfr_find_qh(qh);
	// TODO what is verdict here
	verdict = (*nfr[fr_n].nfr_wf)(nfr[fr_n].nfr_wfarg, nfr_mapproto(proto),				payload, payloadsize);
	id = ntohl(ph->packet_id);
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
