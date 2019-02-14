#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <time.h>

#include "spin_cfg.h"

#include "netlink_commands.h"
#include "spin_list.h"
#include "node_cache.h"

#include "spind.h"
#include "mainloop.h"
#include "spin_log.h"

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

static void
wf_do_ip(void *arg, int iplist, int addrem, ip_t *ip_addr) {
    config_command_t cmd;

    switch (iplist) {
    case IPLIST_BLOCK:
        cmd = addrem == SF_ADD ? SPIN_CMD_ADD_BLOCK : SPIN_CMD_REMOVE_BLOCK;
        break;
    case IPLIST_IGNORE:
        cmd = addrem == SF_ADD ? SPIN_CMD_ADD_IGNORE : SPIN_CMD_REMOVE_IGNORE;
        break;
    case IPLIST_ALLOW:
        cmd = addrem == SF_ADD ? SPIN_CMD_ADD_EXCEPT : SPIN_CMD_REMOVE_EXCEPT;
        break;
    }

    // Do not get in the way of iptables block
    if (cmd != SPIN_CMD_ADD_BLOCK) {
        core2kernel_do_ip(cmd, ip_addr);
    }
}

static
void wf_netlink(void *arg, int data, int timeout) {
    int rs;
    message_type_t type;
    time_t now;

    now = time(NULL);
    if (timeout) {

        // do timeout things
        // nothing so far
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
        type = wire2pktinfo(&pkt, (unsigned char *)NLMSG_DATA(traffic_nlh));
#ifdef notdef
        if (type == SPIN_BLOCKED) {
            //pktinfo2str(pkt_str, &pkt, 2048);
            //printf("[BLOCKED] %s\n", pkt_str);
            node_cache_add_pkt_info(node_cache, &pkt, now);
            send_command_blocked(&pkt);
            check_send_ack();
        } else
#endif
        if (type == SPIN_TRAFFIC_DATA) {
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
            // now handled by core2nfq_dns
            check_send_ack();
        } else if (type == SPIN_DNS_QUERY) {
            // now handled by core2nfq_dns
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

int init_netlink(int local, node_cache_t* node_cache_a)
{
    //ssize_t c = 0;
    struct timeval tv;
    uint32_t now = time(NULL);
    static int all_lists[N_IPLIST] = { 1, 1, 1 };

    node_cache = node_cache_a;

    local_mode = local;

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
    spin_register("netlink", wf_do_ip, (void *) 0, all_lists);

    return 0;
}

void cleanup_netlink() {
    close(traffic_sock_fd);
    flow_list_destroy(flow_list);
    free(traffic_nlh);
}
