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

static
void add_ip_to_file(uint8_t* ip, const char* filename) {
    tree_t *ip_tree = tree_create(cmp_ips);
    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    tree_add(ip_tree, sizeof(ip_t), ip, 0, NULL, 1);
    store_ip_tree(ip_tree, filename);
}

static
void add_ip_tree_to_file(tree_t* tree, const char* filename) {
    tree_entry_t* cur;
    tree_t *ip_tree = tree_create(cmp_ips);

    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_add(ip_tree, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    store_ip_tree(ip_tree, filename);
}

static
void remove_ip_tree_from_file(tree_t* tree, const char* filename) {
    tree_entry_t* cur;
    tree_t *ip_tree = tree_create(cmp_ips);

    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_remove(ip_tree, cur->key_size, cur->key);
        cur = tree_next(cur);
    }
    store_ip_tree(ip_tree, filename);
}

static
void remove_ip_from_file(ip_t* ip, const char* filename) {
    tree_t *ip_tree = tree_create(cmp_ips);
    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    tree_remove(ip_tree, sizeof(ip_t), ip);
    store_ip_tree(ip_tree, filename);
}

// returns the node->ips tree if successful, NULL if node not found
// (this is useful if we have other actions to perform with the node's
// ip addresses, such as print or save them)
// note: caller does *not* get ownership of the tree
// TODO: can we skip the lookup? make all callers do it first
static
tree_t* send_netlink_command_for_node_ips(config_command_t cmd, int node_id) {
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_entry_t* ip_entry;
    netlink_command_result_t* command_result;

    if (node == NULL) {
        return NULL;
    }
    ip_entry = tree_first(node->ips);
    while (ip_entry != NULL) {
        command_result = send_netlink_command_iparg(cmd, ip_entry->key);
        ip_entry = tree_next(ip_entry);
    }
    // should we check result?
    netlink_command_result_destroy(command_result);
    return node->ips;
}

void handle_command_get_list(config_command_t cmd, const char* json_command) {
    netlink_command_result_t* command_result;
    buffer_t* response_json = buffer_create(4096);
    buffer_t* result_json = buffer_create(4096);
    unsigned int response_size;

    // ask the kernel module for the list of ignored nodes
    command_result = send_netlink_command_noarg(cmd);
    if (command_result == NULL) {
        fprintf(stderr, "Error connecting to kernel, is the module running?\n");
        return;
    }
    netlink_command_result2json(command_result, result_json);
    if (!buffer_ok(result_json)) {
        buffer_destroy(result_json);
        buffer_destroy(response_json);
        netlink_command_result_destroy(command_result);
        return;
    }
    buffer_finish(result_json);
    response_size = create_mqtt_command(response_json, json_command, NULL, buffer_str(result_json));
    if (!buffer_ok(response_json)) {
        buffer_destroy(result_json);
        buffer_destroy(response_json);
        netlink_command_result_destroy(command_result);
        return;
    }
    buffer_finish(response_json);
    pubsub_publish(response_size, buffer_str(response_json));

    netlink_command_result_destroy(command_result);
    buffer_destroy(result_json);
    buffer_destroy(response_json);
}


void handle_command_add_filter(int node_id) {
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_ADD_IGNORE, node_id);
    if (ips != NULL) {
        add_ip_tree_to_file(ips, "/etc/spin/ignore.list");
    }
}

void handle_command_remove_ip(config_command_t cmd, ip_t* ip, const char* configfile_to_update) {
    netlink_command_result_t* cr = send_netlink_command_iparg(cmd, ip);
    if (configfile_to_update != NULL) {
        remove_ip_from_file(ip, configfile_to_update);
    }
    netlink_command_result_destroy(cr);

    /*
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_REMOVE_IGNORE, node_id);
    if (ips != NULL) {
        remove_ip_tree_from_file(ips, "/etc/spin/ignore.list");
    }
    */
}

void handle_command_reset_filters() {
    // clear the filters; derive them from our own addresses again
    // hmm, use a script for this?
    //load_ips_from_file
    system("/usr/lib/spin/show_ips.lua -o /etc/spin/ignore.list -f");
    system("spin_config ignore load /etc/spin/ignore.list");
}

void handle_command_add_name(int node_id, char* name) {
    // find the node
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_entry_t* ip_entry;

    if (node == NULL) {
        return;
    }
    node_set_name(node, name);

    // re-read node names, just in case someone has been editing it
    // TODO: make filename configurable? right now it will silently fail
    node_names_read_userconfig(node_cache->names, "/etc/spin/names.conf");

    // if it has a mac address, use that, otherwise, add for all its ip
    // addresses
    if (node->mac != NULL) {
        node_names_add_user_name_mac(node_cache->names, node->mac, name);
    } else {
        ip_entry = tree_first(node->ips);
        while (ip_entry != NULL) {
            node_names_add_user_name_ip(node_cache->names, (ip_t*)ip_entry->key, name);
            ip_entry = tree_next(ip_entry);
        }
    }
    // TODO: make filename configurable? right now it will silently fail
    node_names_write_userconfig(node_cache->names, "/etc/spin/names.conf");
}

void handle_command_block_data(int node_id) {
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_ADD_BLOCK, node_id);
    if (ips != NULL) {
        add_ip_tree_to_file(ips, "/etc/spin/block.list");
    }
    // the is_blocked status is only read if this node had a new ip address added, so update it now
    if (node != NULL) {
        node->is_blocked = 1;
    }
}

void handle_command_stop_block_data(int node_id) {
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_REMOVE_BLOCK, node_id);
    if (ips != NULL) {
        remove_ip_tree_from_file(ips, "/etc/spin/block.list");
    }
    // the is_blocked status is only read if this node had a new ip address added, so update it now
    if (node != NULL) {
        node->is_blocked = 0;
    }
}

void handle_command_allow_data(int node_id) {
    // TODO store this in "/etc/spin/blocked.conf"
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_ADD_EXCEPT, node_id);
    if (ips != NULL) {
        add_ip_tree_to_file(ips, "/etc/spin/allow.list");
    }
    // the is_excepted status is only read if this node had a new ip address added, so update it now
    if (node != NULL) {
        node->is_excepted = 1;
    }
}

void handle_command_stop_allow_data(int node_id) {
    // TODO store this in "/etc/spin/blocked.conf"
    node_t* node = node_cache_find_by_id(node_cache, node_id);
    tree_t* ips = send_netlink_command_for_node_ips(SPIN_CMD_REMOVE_EXCEPT, node_id);
    if (ips != NULL) {
        remove_ip_tree_from_file(ips, "/etc/spin/allow.list");
    }
    // the is_excepted status is only read if this node had a new ip address added, so update it now
    if (node != NULL) {
        node->is_excepted = 0;
    }
}

static
void wf_netlink(int data, int timeout) {
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

    mainloop_register("netlink", wf_netlink, traffic_sock_fd, 0);

#ifdef loop_in_init
    fds[0].fd = traffic_sock_fd;
    fds[0].events = POLLIN;

    fds[1].fd = mosquitto_socket(mosq);
    fds[1].events = POLLIN;

    now = time(NULL);
    last_mosq_poll = now;

    //char str[1024];
    size_t pos = 0;


    /* Read message from kernel */
    while (running) {
        rs = poll(fds, 2, 1000);
        now = time(NULL);
        /*
        memset(str, 0, 1024);
        pos = 0;
        pos += sprintf(&str[pos], "[XX] ");
        pos += sprintf(&str[pos], " rs: %d ", rs);
        pos += sprintf(&str[pos], "last: %u now: %u dif: %u", last_mosq_poll, now, now - last_mosq_poll);
        pos += sprintf(&str[pos], " D: %02x (%d) ", fds[1].revents, fds[1].revents & POLLIN);
        pos += sprintf(&str[pos], " TO: %d", now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME);
        pos += sprintf(&str[pos], "\n");
        spin_log(LOG_DEBUG, "%s", str);
        */

#ifdef notdef
        if (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME / 2) {
            //spin_log(LOG_DEBUG, "Calling loop for keepalive check\n");
            mosquitto_loop_misc(mosq);
            last_mosq_poll = now;
        }
#endif

        if (rs < 0) {
            spin_log(LOG_ERR, "error in poll(): %s\n", strerror(errno));
        } else if (rs == 0) {
	    maybe_sendflow(flow_list, now);
        } else {
            if (fds[0].revents) {
                if (fds[0].revents & POLLIN) {

#ifdef notdef
            if ((fds[1].revents & POLLIN)
#ifdef notdef
	    	|| (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME)
#endif
						) {
                //spin_log(LOG_DEBUG, "Calling loop for data\n");
                mosquitto_loop(mosq, 0, 10);
                last_mosq_poll = now;
            } else if (fds[1].revents) {
                spin_log(LOG_ERR, "Unexpected result from mosquitto socket (%d)\n", fds[1].revents);
                spin_log(LOG_ERR, "Socket fd: %d, mosq struct has %d\n", fds[1].fd, mosquitto_socket(mosq));
                if (stop_on_error) {
                    exit(1);
                }
                //usleep(500000);
                spin_log(LOG_ERR, "Reconnecting to mosquitto server\n");
                connect_mosquitto(mosq_host, mosq_port);
                spin_log(LOG_ERR, " Reconnected, mosq fd now %d\n", mosquitto_socket(mosq));

            }
#endif
        }
    }

#endif /* loop_in_init */
    return 0;
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

cleanup_netlink() {

    close(traffic_sock_fd);
    flow_list_destroy(flow_list);
    free(traffic_nlh);
    cleanup_cache();
}
