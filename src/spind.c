#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>

#include "pkt_info.h"
#include "dns_cache.h"
#include "node_cache.h"
#include "tree.h"
#include "netlink_commands.h"
#include "spin_log.h"

// perhaps remove
#include "spin_cfg.h"

#include <poll.h>

#include <mosquitto.h>

#include <signal.h>
#include <time.h>

#include "jsmn.h"

#include "version.h"

#define NETLINK_CONFIG_PORT 30
#define NETLINK_TRAFFIC_PORT 31

#define MOSQUITTO_KEEPALIVE_TIME 60

#define MAX_NETLINK_PAYLOAD 1024 /* maximum payload size*/
struct sockaddr_nl src_addr, dest_addr;
struct nlmsghdr *traffic_nlh = NULL;
struct iovec traffic_iov;
int traffic_sock_fd;
struct msghdr traffic_msg;

int ack_counter;
int stop_on_error;

static dns_cache_t* dns_cache;
static node_cache_t* node_cache;
static struct mosquitto* mosq;

static int running;
static int local_mode;

const char* mosq_host;
int mosq_port;

#define MQTT_CHANNEL_TRAFFIC "SPIN/traffic"
#define MQTT_CHANNEL_COMMANDS "SPIN/commands"

void send_ack()
{
    uint32_t now = time(NULL);

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

    dns_cache_clean(dns_cache, now);
    // hey, how about we clean here?_
}

#define MESSAGES_BEFORE_PING 50
void check_send_ack() {
    ack_counter++;
    if (ack_counter > MESSAGES_BEFORE_PING) {
        send_ack();
        ack_counter = 0;
    }
}

static inline void
print_pktinfo_wirehex(pkt_info_t* pkt_info) {
    uint8_t* wire = (char*)malloc(46);
    memset(wire, 0, 46);
    pktinfo2wire(wire, pkt_info);
    // size is 49 (46 bytes of pkt info, 1 byte of type, 2 bytes of size)
    int i;
    // print version, msg_type, and size (which is irrelevant right now)
    printf("{ 0x01, 0x01, 0x00, 0x00, ");
    for (i = 0; i < 45; i++) {
        printf("0x%02x, ", wire[i]);
    }
    printf("0x%02x }\n", wire[45]);

    free(wire);
}

static inline void
print_dnspktinfo_wirehex(dns_pkt_info_t* pkt_info) {
    uint8_t* wire = (char*)malloc(277);
    memset(wire, 0, 277);
    dns_pktinfo2wire(wire, pkt_info);
    // size is 49 (46 bytes of pkt info, 1 byte of type, 2 bytes of size)
    int i;
    // print version, msg_type, and size (which is irrelevant right now)
    printf("{ 0x01, 0x02, 0x00, 0x00, ");
    for (i = 0; i < 276; i++) {
        printf("0x%02x, ", wire[i]);
    }
    printf("0x%02x }\n", wire[277]);

    free(wire);
}

unsigned int netlink_command_result2json(netlink_command_result_t* command_result, buffer_t* buf) {
    unsigned int s = 0;
    int i;
    char ip_str[INET6_ADDRSTRLEN];

    if (command_result->error != NULL) {
        buffer_write(buf, "\"%s\", ", command_result->error);
    } else {
        buffer_write(buf, " [ ");
        for (i = 0; i < command_result->ip_count; i++) {
            spin_ntop(ip_str, &command_result->ips[i], INET6_ADDRSTRLEN);

            buffer_write(buf, "\"%s\" ", ip_str);
            if (i < command_result->ip_count - 1) {
                buffer_write(buf, ", ");
            }
        }
        buffer_write(buf, "] ");
    }
    return s;
}

unsigned int create_mqtt_command(buffer_t* buf, const char* command, char* argument, char* result) {
    buffer_write(buf, "{ \"command\": \"%s\"", command);
    if (argument != NULL) {
        buffer_write(buf, ", \"argument\": %s", argument);
    }
    if (result != NULL) {
        buffer_write(buf, ", \"result\": %s", result);
    }
    buffer_write(buf, " }");
    return buf->pos;
}

void send_command_blocked(pkt_info_t* pkt_info) {
    unsigned int response_size;
    buffer_t* response_json = buffer_create(2048);
    buffer_t* pkt_json = buffer_create(2048);
    unsigned int p_size;

    p_size = pkt_info2json(node_cache, pkt_info, pkt_json);
    buffer_finish(pkt_json);
    response_size = create_mqtt_command(response_json, "blocked", NULL, buffer_str(pkt_json));
    if (buffer_finish(response_json)) {
        mosquitto_publish(mosq, NULL, "SPIN/traffic", response_size, buffer_str(response_json), 0, false);
    } else {
        spin_log(LOG_WARNING, "Error converting blocked pkt_info to JSON; partial packet: %s\n", buffer_str(response_json));
    }
    buffer_destroy(response_json);
    buffer_destroy(pkt_json);
}

void send_command_dnsquery(dns_pkt_info_t* pkt_info) {
    unsigned int response_size;
    buffer_t* response_json = buffer_create(2048);
    buffer_t* pkt_json = buffer_create(2048);
    unsigned int p_size;

    spin_log(LOG_DEBUG, "[XX] jsonify that pkt info to get dns query command\n");
    p_size = dns_query_pkt_info2json(node_cache, pkt_info, pkt_json);
    if (p_size > 0) {
        spin_log(LOG_DEBUG, "[XX] got an actual dns query command (size >0)\n");
        buffer_finish(pkt_json);
        response_size = create_mqtt_command(response_json, "dnsquery", NULL, buffer_str(pkt_json));
        if (buffer_finish(response_json)) {
            mosquitto_publish(mosq, NULL, "SPIN/traffic", response_size, buffer_str(response_json), 0, false);
        } else {
            spin_log(LOG_WARNING, "Error converting dnsquery pkt_info to JSON; partial packet: %s\n", buffer_str(response_json));
        }
    } else {
        spin_log(LOG_DEBUG, "[XX] did not get an actual dns query command (size 0)\n");
    }
    buffer_destroy(response_json);
    buffer_destroy(pkt_json);
}

// function definition below
void connect_mosquitto(const char* host, int port);

int init_netlink()
{
    ssize_t c = 0;
    int rs;
    message_type_t type;
    struct timeval tv;
    struct pollfd fds[2];
    uint32_t now, last_mosq_poll;

    buffer_t* json_buf = buffer_create(4096);
    buffer_allow_resize(json_buf);
    int mosq_result;

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

    fds[0].fd = traffic_sock_fd;
    fds[0].events = POLLIN;

    fds[1].fd = mosquitto_socket(mosq);
    fds[1].events = POLLIN;

    now = time(NULL);
    last_mosq_poll = now;
    flow_list_t* flow_list = flow_list_create(time(NULL));

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

        if (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME / 2) {
            //spin_log(LOG_DEBUG, "Calling loop for keepalive check\n");
            mosquitto_loop_misc(mosq);
            last_mosq_poll = now;
        }

        if (rs < 0) {
            spin_log(LOG_ERR, "error in poll(): %s\n", strerror(errno));
        } else if (rs == 0) {
            if (flow_list_should_send(flow_list, now)) {
                if (!flow_list_empty(flow_list)) {
                    // create json, send it
                    buffer_reset(json_buf);
                    create_traffic_command(node_cache, flow_list, json_buf, now);
                    if (buffer_finish(json_buf)) {
                        mosq_result = mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
                    }
                }
                flow_list_clear(flow_list, now);
            }
        } else {
            if (fds[0].revents) {
                if (fds[0].revents & POLLIN) {
                    rs = recvmsg(traffic_sock_fd, &traffic_msg, 0);

                    if (rs < 0) {
                        continue;
                    }
                    c++;
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
                            continue;
                        }

                        if (flow_list_should_send(flow_list, now)) {
                            if (!flow_list_empty(flow_list)) {
                                // create json, send it
                                buffer_reset(json_buf);
                                create_traffic_command(node_cache, flow_list, json_buf, now);
                                if (buffer_finish(json_buf)) {
                                    mosq_result = mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
                                }
                            }
                            flow_list_clear(flow_list, now);
                        } else {
                            // add the current one
                            flow_list_add_pktinfo(flow_list, &pkt);
                        }
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
                } else {
                    spin_log(LOG_ERR, "Unexpected result from netlink socket (%d)\n", fds[0].revents);
                    usleep(500000);
                }
            }

            if ((fds[1].revents & POLLIN) || (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME)) {
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
        }
    }
    close(traffic_sock_fd);
    flow_list_destroy(flow_list);
    buffer_destroy(json_buf);
    return 0;
}


static int json_dump(const char *js, jsmntok_t *t, size_t count, int indent) {
    int i, j, k;
    if (count == 0) {
        return 0;
    }
    if (t->type == JSMN_PRIMITIVE) {
        printf("%.*s", t->end - t->start, js+t->start);
        return 1;
    } else if (t->type == JSMN_STRING) {
        printf("'%.*s'", t->end - t->start, js+t->start);
        return 1;
    } else if (t->type == JSMN_OBJECT) {
        printf("\n");
        j = 0;
        for (k = 0; k < indent; k++) printf("  ");
        printf("{\n");
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent; k++) printf("  ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf(": ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf(" \n");
        }
        for (k = 0; k < indent; k++) printf("  ");
        printf("}");
        return j+1;
    } else if (t->type == JSMN_ARRAY) {
        j = 0;
        printf("\n");
        for (i = 0; i < t->size; i++) {
            for (k = 0; k < indent-1; k++) printf("  ");
            printf("   - ");
            j += json_dump(js, t+1+j, count-j, indent+1);
            printf("\n");
        }
        return j+1;
    }
    return 0;
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
    mosquitto_publish(mosq, NULL, "SPIN/traffic", response_size, buffer_str(response_json), 0, false);

    netlink_command_result_destroy(command_result);
    buffer_destroy(result_json);
    buffer_destroy(response_json);
}

void add_ip_to_file(uint8_t* ip, const char* filename) {
    tree_t *ip_tree = tree_create(cmp_ips);
    // if this fails, we simply try to write a new one anyway
    read_ip_tree(ip_tree, filename);
    tree_add(ip_tree, sizeof(ip_t), ip, 0, NULL, 1);
    store_ip_tree(ip_tree, filename);
}

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

void send_command_restart() {
    buffer_t* response_json = buffer_create(4096);
    unsigned int response_size = create_mqtt_command(response_json, "serverRestart", NULL, NULL);
    buffer_finish(response_json);
    if (buffer_ok(response_json)) {
        mosquitto_publish(mosq, NULL, "SPIN/traffic", response_size, buffer_str(response_json), 0, false);
    } else {
        spin_log(LOG_ERR, "error: response size too large\n");
    }
    buffer_destroy(response_json);
}

// returns 1 on success, 0 on error
int json_parse_int_arg(int* dest,
                       const char* json_str,
                       jsmntok_t* tokens,
                       int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    if (token.type != JSMN_PRIMITIVE) {
        return 0;
    } else {
        *dest = atoi(json_str + token.start);
        return 1;
    }
}

int json_parse_string_arg(char* dest,
                          size_t dest_size,
                          const char* json_str,
                          jsmntok_t* tokens,
                          int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    size_t size;
    if (token.type != JSMN_STRING) {
        return 0;
    } else {
        size = snprintf(dest, dest_size, "%.*s", token.end - token.start, json_str+token.start);
        return 1;
    }
}

int json_parse_ip_arg(ip_t* dest,
                      const char* json_str,
                      jsmntok_t* tokens,
                      int argument_token_i) {
    char ip_str[INET6_ADDRSTRLEN];
    int result = json_parse_string_arg(ip_str, INET6_ADDRSTRLEN, json_str, tokens, argument_token_i);
    if (!result) {
        return result;
    } else {
        result = spin_pton(dest, ip_str);
        if (!result) {
            return 0;
        } else {
            return 1;
        }
    }
}

// more complex argument, of the form
// { "node_id": <int>, "name": <str> }
int json_parse_node_id_name_arg(int* node_id,
                                char* name,
                                size_t name_size,
                                const char* json_str,
                                jsmntok_t* tokens,
                                int argument_token_i) {
    jsmntok_t token = tokens[argument_token_i];
    int i, i_n, i_v, result, id_found = 0, name_found = 0;

    if (token.type != JSMN_OBJECT ||
        token.size != 2) {
        return 0;
    } else {
        for (i = 0; i < token.size; i++) {
            // i*2 is name, i*2 + 1 is value
            i_n = argument_token_i + i*2 + 1;
            i_v = argument_token_i + i*2 + 2;
            if (strncmp("name", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
                result = json_parse_string_arg(name, name_size, json_str, tokens, i_v);
                if (!result) {
                    return result;
                }
                name_found = 1;
            } else if (strncmp("node_id", json_str + tokens[i_n].start, tokens[i_n].end-tokens[i_n].start) == 0) {
                result = json_parse_int_arg(node_id, json_str, tokens, i_v);
                if (!result) {
                    return result;
                }
                id_found = 1;
            }
        }
    }
    return (name_found && id_found);
}

void handle_json_command_2(size_t cmd_name_len,
                           const char* cmd_name,
                           const char* json_str,
                           jsmntok_t* tokens,
                           int argument_token_i) {
    // figure out which command we got; depending on that we'll
    // parse the arguments (if any), and handle directly, or contact
    // the kernel module if necessary
    config_command_t cmd;
    int node_id_arg = 0;
    int result;
    char str_arg[80];
    ip_t ip_arg;
    // in a few cases, we need to update the node cache
    node_t* node;

    if (strncmp(cmd_name, "get_filters", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "get_blocks", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "get_alloweds", cmd_name_len) == 0) {
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "add_filter", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_add_filter(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "remove_filter", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_IGNORE, &ip_arg, "/etc/spin/ignore.list");
        }
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    } else if (strncmp(cmd_name, "reset_filters", cmd_name_len) == 0) {
        handle_command_reset_filters();
        handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    //} else if (strncmp(cmd_name, "get_names", cmd_name_len) == 0) {
    //    handle_command_get_names();
    } else if (strncmp(cmd_name, "add_name", cmd_name_len) == 0) {
        if (json_parse_node_id_name_arg(&node_id_arg, str_arg, 24, json_str, tokens, argument_token_i)) {
            handle_command_add_name(node_id_arg, str_arg);
        }
    } else if (strncmp(cmd_name, "add_block_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_block_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "remove_block_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_stop_block_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "remove_block_ip", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_BLOCK, &ip_arg, "/etc/spin/block.list");
            node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip_arg);
            if (node) {
                node->is_blocked = 0;
            }
        }
        handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    } else if (strncmp(cmd_name, "add_allow_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_allow_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "remove_allow_node", cmd_name_len) == 0) {
        if (json_parse_int_arg(&node_id_arg, json_str, tokens, argument_token_i)) {
            handle_command_stop_allow_data(node_id_arg);
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    } else if (strncmp(cmd_name, "remove_allow_ip", cmd_name_len) == 0) {
        if (json_parse_ip_arg(&ip_arg, json_str, tokens, argument_token_i)) {
            handle_command_remove_ip(SPIN_CMD_REMOVE_EXCEPT, &ip_arg, "/etc/spin/allow.list");
            node = node_cache_find_by_ip(node_cache, sizeof(ip_t), &ip_arg);
            if (node) {
                node->is_excepted = 0;
            }
        }
        handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    }
}

void handle_json_command(const char* data) {
    jsmn_parser p;
    // todo: alloc these upon global init, realloc when necessary?
    size_t tok_count = 10;
    jsmntok_t tokens[10];
    int result;

    jsmn_init(&p);
    result = jsmn_parse(&p, data, strlen(data), tokens, 10);
    if (result < 0) {
        spin_log(LOG_ERR, "Error: unable to parse json data: %d\n", result);
        return;
    }
    // token should be object, first child should be "command":
    if (tokens[0].type != JSMN_OBJECT) {
        spin_log(LOG_ERR, "Error: unknown json data\n");
        return;
    }
    // token 1 should be "command",
    // token 2 should be the command name (e.g. "get_filters")
    // token 3 should be "arguments",
    // token 4 should be an object with the arguments (possibly empty)
    if (tokens[1].type != JSMN_STRING || strncmp(data+tokens[1].start, "command", 7) != 0) {
        spin_log(LOG_ERR, "Error: json data not command\n");
        return;
    }
    if (tokens[3].type != JSMN_STRING || strncmp(data+tokens[3].start, "argument", 7) != 0) {
        spin_log(LOG_ERR, "Error: json data does not contain argument field\n");
        return;
    }
    handle_json_command_2(tokens[2].end - tokens[2].start, data+tokens[2].start,
                          data, tokens, 4);
}

/*
void handle_command(const struct mosquitto_message* msg, void* user_data) {
    // commands are in json format of the form:
    // { "command": <command name> (string)
    //   "arguments": <arguments> (type depends on command)
    // we should
}
*/

void on_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {
    if (strcmp(msg->topic, MQTT_CHANNEL_COMMANDS) == 0) {
        handle_json_command(msg->payload);
    }
}

void connect_mosquitto(const char* host, int port) {
    const char* client_name = "asdf";
    int result;

    if (mosq != NULL) {
        mosquitto_disconnect(mosq);
        mosquitto_destroy(mosq);
    }

    mosq = mosquitto_new(client_name, 1, NULL);
    spin_log(LOG_INFO, "Connecting to mqtt server on %s:%d\n", host, port);
    result = mosquitto_connect(mosq, host, port, MOSQUITTO_KEEPALIVE_TIME);
    if (result != 0) {
        spin_log(LOG_ERR, "Error connecting to mqtt server on %s:%d, %s\n", host, port, mosquitto_strerror(result));
        exit(1);
    }
    spin_log(LOG_INFO, "Connected to mqtt server on %s:%d with keepalive value %d\n", host, port, MOSQUITTO_KEEPALIVE_TIME);
    result = mosquitto_subscribe(mosq, NULL, MQTT_CHANNEL_COMMANDS, 0);
    if (result != 0) {
        spin_log(LOG_ERR, "Error subscribing to topic %s: %s\n", MQTT_CHANNEL_COMMANDS, mosquitto_strerror(result));
    }

    mosquitto_message_callback_set(mosq, on_message);

}

void init_mosquitto(const char* host, int port) {
    mosquitto_lib_init();

    connect_mosquitto(host, port);

    send_command_restart();
    handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
}

void init_cache() {
    dns_cache = dns_cache_create();
    node_cache = node_cache_create();
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

void int_handler(int signal) {
    if (running) {
        spin_log(LOG_INFO, "Got interrupt, quitting\n");
        running = 0;
    } else {
        spin_log(LOG_INFO, "Got interrupt again, hard exit\n");
        exit(0);
    }
}

void print_version() {
    printf("SPIN daemon version %s\n", BUILD_VERSION);
    printf("Build date: %s\n", BUILD_DATE);
}

void log_version() {
    spin_log(LOG_INFO, "SPIN daemon version %s started\n", BUILD_VERSION);
    spin_log(LOG_INFO, "Build date: %s\n", BUILD_DATE);
}

void print_help() {
    printf("Usage: spind [options]\n");
    printf("Options:\n");
    printf("-d\t\t\tlog debug messages (set log level to LOG_DEBUG)");
    printf("-h\t\t\tshow this help\n");
    printf("-l\t\t\trun in local mode (do not check for ARP cache entries)\n");
    printf("-o\t\t\tlog to stdout instead of syslog\n");
    printf("-m <address>\t\t\tHostname or IP address of the MQTT server\n");
    printf("-p <port number>\t\t\tPort number of the MQTT server\n");
    printf("-v\t\t\tprint the version of spind and exit\n");
}

int main(int argc, char** argv) {
    int result;
    int c;
    int log_verbosity = 6;
    int use_syslog = 1;
    mosq_host = "127.0.0.1";
    mosq_port = 1883;
    stop_on_error = 0;

    while ((c = getopt (argc, argv, "dehlm:op:v")) != -1) {
        switch (c) {
        case 'd':
            log_verbosity = 7;
            break;
        case 'e':
            stop_on_error = 1;
            break;
        case 'h':
            print_help();
            exit(0);
            break;
        case 'l':
            spin_log(LOG_INFO, "Running in local mode; traffic without either entry in arp cache will be shown too\n");
            local_mode = 1;
            break;
        case 'm':
            mosq_host = optarg;
            break;
        case 'o':
            printf("Logging to stdout instead of syslog\n");
            use_syslog = 0;
            break;
        case 'p':
            mosq_port = strtol(optarg, NULL, 10);
            if (mosq_port <= 0 || mosq_port > 65535) {
                fprintf(stderr, "Error, not a valid port number: %s\n", optarg);
                exit(1);
            }
            break;
        case 'v':
            print_version();
            exit(0);
            break;
        default:
            abort ();
        }
    }

    spin_log_init(use_syslog, log_verbosity, "spind");
    log_version();

    init_cache();
    init_mosquitto(mosq_host, mosq_port);
    signal(SIGINT, int_handler);

    running = 1;
    result = init_netlink();

    cleanup_cache();
    free(traffic_nlh);

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}
