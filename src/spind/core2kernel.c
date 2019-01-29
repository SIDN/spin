#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <time.h>

#include "spin_cfg.h"

#include "dns_cache.h"
#include "tree.h"
#include "netlink_commands.h"
#include "node_cache.h"

#include "mainloop.h"
#include "spin_log.h"

static dns_cache_t* dns_cache;
extern node_cache_t* node_cache;

#define NETLINK_CONFIG_PORT 30
#define NETLINK_TRAFFIC_PORT 31

#define MAX_NETLINK_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *traffic_nlh = NULL;
struct iovec traffic_iov;
int traffic_sock_fd;
struct msghdr traffic_msg;

static int local_mode;

flow_list_t* flow_list;

int ack_counter;
static
void send_ack()
{

    memset(traffic_nlh, 0, NLMSG_SPACE(MAX_NETLINK_PAYLOAD));
    traffic_nlh->nlmsg_len = NLMSG_SPACE(MAX_NETLINK_PAYLOAD);
    traffic_nlh->nlmsg_pid = getpid();
    traffic_nlh->nlmsg_flags = 0;

    strcpy(NLMSG_DATA(traffic_nlh), "Hello!");

    traffic_iov.iov_base = (void *)traffic_nlh;
    traffic_iov.iov_len = traffic_nlh->nlmsg_len;
    traffic_msg.msg_name = (void *)&dest_addr;
    traffic_msg.msg_namelen = sizeof(dest_addr);
    traffic_msg.msg_iov = &traffic_iov;
    traffic_msg.msg_iovlen = 1;

    sendmsg(traffic_sock_fd, &traffic_msg, 0);

    dns_cache_clean(dns_cache);
    // hey, how about we clean here?_
}

#define MESSAGES_BEFORE_PING 50
static
void check_send_ack() {
    ack_counter++;
    if (ack_counter > MESSAGES_BEFORE_PING) {
        send_ack();
        ack_counter = 0;
    }
}

#ifdef notdef
static
void add_ip_to_file(uint8_t* ip, const char* filename) {
    tree_t *ip_tree = tree_create(cmp_ips);
    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    tree_add(ip_tree, sizeof(ip_t), ip, 0, NULL, 1);
    store_ip_tree(ip_tree, filename);
}
#endif

int core2kernel_do(config_command_t cmd) {
    netlink_command_result_t* cr;
    
    cr = send_netlink_command_noarg(cmd);
    netlink_command_result_destroy(cr);
    return cr != NULL;
}

int core2kernel_do_ip(config_command_t cmd, ip_t* ip) {
    netlink_command_result_t* cr;
    
    cr = send_netlink_command_iparg(cmd, ip);
    netlink_command_result_destroy(cr);
    return cr != NULL;
}

static
void wf_netlink(void *arg, int data, int timeout) {
    int rs;
    message_type_t type;
    time_t now;

    now = time(NULL);
    if (timeout) {

	// do timeout things
    }
    if (data) {
	rs = recvmsg(traffic_sock_fd, &traffic_msg, 0);

	if (rs < 0) {
	    return;
	}
	// c++;
	//printf("C: %u RS: %u\n", c, rs);
	//printf("Received message payload: %s\n", (char *)NLMSG_DATA(nlh));
	pkt_info_t pkt;
	dns_pkt_info_t dns_pkt;
	char pkt_str[2048];
	type = wire2pktinfo(&pkt, (unsigned char *)NLMSG_DATA(traffic_nlh));
	if (type == SPIN_BLOCKED) {
	    //pktinfo2str(pkt_str, &pkt, 2048);
	    //printf("[BLOCKED] %s\n", pkt_str);
	    node_cache_add_pkt_info(node_cache, &pkt, now);
	    send_command_blocked(&pkt);
	    check_send_ack();
	} else if (type == SPIN_TRAFFIC_DATA) {
	    //pktinfo2str(pkt_str, &pkt, 2048);
	    //printf("[TRAFFIC] %s\n", pkt_str);
	    //print_pktinfo_wirehex(&pkt);
	    node_cache_add_pkt_info(node_cache, &pkt, now);

	    // small experiment; check if either endpoint is an internal device, if not,
	    // skip reporting it
	    // (if this is useful, we should do this check in add_pkt_info above, probably)
	    ip_t ip;
	    node_t* src_node;
	    node_t* dest_node;
	    ip.family = pkt.family;
	    memcpy(ip.addr, pkt.src_addr, 16);
	    src_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
	    memcpy(ip.addr, pkt.dest_addr, 16);
	    dest_node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip);
	    if (src_node == NULL || dest_node == NULL || (src_node->mac == NULL && dest_node->mac == NULL && !local_mode)) {
		return;
	    }

	    maybe_sendflow(flow_list, now);
	    // add the current one
	    flow_list_add_pktinfo(flow_list, &pkt);
	    check_send_ack();
	} else if (type == SPIN_DNS_ANSWER) {
	    // note: bad version would have been caught in wire2pktinfo
	    // in this specific case
	    wire2dns_pktinfo(&dns_pkt, (unsigned char *)NLMSG_DATA(traffic_nlh));

	    // DNS answers are not relayed as traffic; we only
	    // store them internally, so later traffic can be
	    // matched to the DNS answer by IP address lookup
	    dns_cache_add(dns_cache, &dns_pkt, now);
	    node_cache_add_dns_info(node_cache, &dns_pkt, now);
	    // TODO do we need to send nodeUpdate?
	    check_send_ack();
	} else if (type == SPIN_DNS_QUERY) {
	    // We do want to relay dns query information to
	    // clients; it should be sent as command of
	    // type 'dnsquery'


	    // the info now contains:
	    // - domain name queried
	    // - ip address doing the query
	    // - 0 ttl value
	    wire2dns_pktinfo(&dns_pkt, (unsigned char *)NLMSG_DATA(traffic_nlh));
	    // XXXXX this would add wrong ip
	    // If the queried domain name isn't known, we add it as a new node
	    // (with only a domain name)
	    node_cache_add_dns_query_info(node_cache, &dns_pkt, now);
	    //node_cache_add_pkt_info(node_cache, &dns_pkt, now, 1);
	    // We do send a separate notification for the clients that are interested
	    send_command_dnsquery(&dns_pkt);


	    // TODO do we need to send nodeUpdate?
	    check_send_ack();
	} else if (type == SPIN_ERR_BADVERSION) {
	    printf("Error: version mismatch between client and kernel module\n");
	} else {
	    printf("unknown type? %u\n", type);
	}
    }
#ifdef notdef
    else {
	spin_log(LOG_ERR, "Unexpected result from netlink socket (%d)\n", fds[0].revents);
	usleep(500000);
    }
#endif
}

void init_cache() {
    dns_cache = dns_cache_create();
    node_cache = node_cache_create();
}

int init_netlink(int local)
{
    //ssize_t c = 0;
    int rs;
    message_type_t type;
    struct timeval tv;
#ifdef notdef
    struct pollfd fds[2];
#endif
    uint32_t now, last_mosq_poll;


    local_mode = local;
    init_cache();

    traffic_nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_NETLINK_PAYLOAD));

    ack_counter = 0;

    traffic_sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_TRAFFIC_PORT);
    if(traffic_sock_fd < 0) {
        spin_log(LOG_ERR, "Error connecting to netlink socket: %s\n", strerror(errno));
        return -1;
    }

    tv.tv_sec = 0;
    tv.tv_usec = 500;
    setsockopt(traffic_sock_fd, 270, SO_RCVTIMEO, (const char*)&tv,sizeof(struct timeval));

    memset(&src_addr, 0, sizeof(src_addr));
    src_addr.nl_family = AF_NETLINK;
    src_addr.nl_pid = getpid(); /* self pid */

    bind(traffic_sock_fd, (struct sockaddr*)&src_addr, sizeof(src_addr));

    memset(&dest_addr, 0, sizeof(dest_addr));
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.nl_family = AF_NETLINK;
    dest_addr.nl_pid = 0; /* For Linux Kernel */
    dest_addr.nl_groups = 0; /* unicast */

    send_ack();

    flow_list = flow_list_create(now);

    mainloop_register("netlink", wf_netlink, (void *) 0, traffic_sock_fd, 0);

    return 0;
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

void cleanup_netlink() {

    close(traffic_sock_fd);
    flow_list_destroy(flow_list);
    free(traffic_nlh);
    cleanup_cache();
}
