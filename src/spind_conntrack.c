#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <arpa/inet.h>

#include <libmnl/libmnl.h>
#include <libnetfilter_conntrack/libnetfilter_conntrack.h>
#include <kernelmod/pkt_info.h>

#include <signal.h>

#include <mosquitto.h>
#include "dns_cache.h"
#include "node_cache.h"
#include "spin_log.h"

#include "version.h"

#include <libnetfilter_queue/libnetfilter_queue.h>

#define MOSQUITTO_KEEPALIVE_TIME 60
#define MQTT_CHANNEL_TRAFFIC "SPIN/traffic"
#define MQTT_CHANNEL_COMMANDS "SPIN/commands"

#include <poll.h>

static dns_cache_t* dns_cache;
static node_cache_t* node_cache;
static struct mosquitto* mosq;

static int running;
static int local_mode;

const char* mosq_host;
int mosq_port;
int stop_on_error;


typedef struct {
  flow_list_t* flow_list;
  node_cache_t* node_cache;
} cb_data_t;



static inline u_int16_t get_u16_attr(struct nf_conntrack *ct, char ATTR) {
  return ntohs(nfct_get_attr_u16(ct, ATTR));
}

static inline u_int32_t get_u32_attr(struct nf_conntrack *ct, char ATTR) {
  return ntohl(nfct_get_attr_u32(ct, ATTR));
}

static inline u_int64_t get_u64_attr(struct nf_conntrack *ct, char ATTR) {
  return nfct_get_attr_u64(ct, ATTR);
}

int nfct_to_pkt_info(pkt_info_t* pkt_info_orig, pkt_info_t* pkt_info_reply, struct nf_conntrack *ct) {
  unsigned int offset = 0;
  u_int32_t tmp;
  
  pkt_info_orig->family = nfct_get_attr_u8(ct, ATTR_ORIG_L3PROTO);
  switch (pkt_info_orig->family) {
  case AF_INET:
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_SRC);
    memset(pkt_info_orig->src_addr, 0, 12);
    memset(pkt_info_orig->dest_addr, 0, 12);
    memcpy((pkt_info_orig->src_addr) + 12, &tmp, 4);
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_DST);
    memcpy((pkt_info_orig->dest_addr) + 12, &tmp, 4);
    pkt_info_orig->src_port = get_u16_attr(ct, ATTR_ORIG_PORT_SRC);
    pkt_info_orig->dest_port = get_u16_attr(ct, ATTR_ORIG_PORT_DST);
    pkt_info_orig->payload_size = get_u64_attr(ct, ATTR_ORIG_COUNTER_BYTES);
    pkt_info_orig->packet_count = nfct_get_attr_u64(ct, ATTR_ORIG_COUNTER_PACKETS);
    pkt_info_orig->payload_size += get_u64_attr(ct, ATTR_REPL_COUNTER_BYTES);
    pkt_info_orig->packet_count += nfct_get_attr_u64(ct, ATTR_REPL_COUNTER_PACKETS);
    pkt_info_orig->payload_offset = 0;

    /*
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_SRC);
    memset(pkt_info_reply->src_addr, 0, 12);
    memset(pkt_info_reply->dest_addr, 0, 12);
    memcpy((pkt_info_reply->src_addr) + 12, &tmp, 4);
    tmp = nfct_get_attr_u32(ct, ATTR_IPV4_DST);
    memcpy((pkt_info_reply->dest_addr) + 12, &tmp, 4);
    pkt_info_reply->src_port = get_u16_attr(ct, ATTR_REPL_PORT_SRC);
    pkt_info_reply->dest_port = get_u16_attr(ct, ATTR_REPL_PORT_DST);
    pkt_info_reply->payload_size = get_u64_attr(ct, ATTR_REPL_COUNTER_BYTES);
    pkt_info_reply->packet_count = nfct_get_attr_u64(ct, ATTR_REPL_COUNTER_PACKETS);
    pkt_info_reply->payload_offset = 0;
    */
    pkt_info_reply->src_port = get_u16_attr(ct, ATTR_REPL_PORT_SRC);
    pkt_info_reply->dest_port = get_u16_attr(ct, ATTR_REPL_PORT_DST);

    break;
  case AF_INET6:
    memcpy((&pkt_info_orig->src_addr[0]), nfct_get_attr(ct, ATTR_IPV6_SRC), 16);
    memcpy((&pkt_info_orig->dest_addr[0]) + 12, nfct_get_attr(ct, ATTR_IPV6_DST), 16);
    pkt_info_orig->src_port = get_u16_attr(ct, ATTR_ORIG_PORT_SRC);
    pkt_info_orig->dest_port = get_u16_attr(ct, ATTR_ORIG_PORT_DST);
    pkt_info_orig->payload_size = get_u64_attr(ct, ATTR_ORIG_COUNTER_BYTES);
    pkt_info_orig->packet_count = nfct_get_attr_u64(ct, ATTR_ORIG_COUNTER_PACKETS);
    pkt_info_orig->payload_size += get_u64_attr(ct, ATTR_REPL_COUNTER_BYTES);
    pkt_info_orig->packet_count += nfct_get_attr_u64(ct, ATTR_REPL_COUNTER_PACKETS);
    pkt_info_orig->payload_offset = 0;
    break;
    // note: ipv6 is u128
  }
}

void do_flow_list(flow_list_t* flow_list, uint32_t now, pkt_info_t* pkt) {
  buffer_t* json_buf = buffer_create(4096);

  flow_list_add_pktinfo(flow_list, pkt);
  buffer_reset(json_buf);
  create_traffic_command(node_cache, flow_list, json_buf, now);
  if (buffer_finish(json_buf)) {
      mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
  }
  flow_list_clear(flow_list, now);
}
/*
void do_flow_list(flow_list_t* flow_list, uint32_t now, pkt_info_t* pkt) {
  buffer_t* json_buf = buffer_create(4096);

  if (flow_list_should_send(flow_list, now)) {
      printf("[XX] SEND FLOW LIST\n");
      if (!flow_list_empty(flow_list)) {
          // create json, send it
          buffer_reset(json_buf);
          create_traffic_command(node_cache, flow_list, json_buf, now);
          if (buffer_finish(json_buf)) {
              mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
          }
      }
      flow_list_clear(flow_list, now);
  } else {
      // add the current one
      printf("[XX] ADD TO FLOW LIST\n");
      flow_list_add_pktinfo(flow_list, pkt);
  }
}
*/

void phexdump(uint8_t* data, unsigned int size) {
    unsigned int i;
    printf("00: ");
    for (i = 0; i < size; i++) {
        if (i > 0 && i % 10 == 0) {
            printf("\n%u: ", i);
        }
        printf("%02x ", data[i]);
    }
    printf("\n");
}

/* this function is called for each flow info data */
/* *data is set by the register function mnl_cb_run */
static int conntrack_cb(const struct nlmsghdr *nlh, void *data)
{
  struct nf_conntrack *ct;
  char buf[4096];
  pkt_info_t orig, reply;
  char new_buf[4096];
  cb_data_t* cb_data = (cb_data_t*) data;
  //flow_list_t* flow_list = cb_data->flow_list;
  // TODO: remove time() calls, use the single one at caller
  uint32_t now = time(NULL);

  ct = nfct_new();
  if (ct == NULL)
    return MNL_CB_OK;

  nfct_nlmsg_parse(nlh, ct);

  //nfct_snprintf(buf, sizeof(buf), ct, NFCT_T_UNKNOWN, NFCT_O_DEFAULT, 0);
  //printf("[XX] %s\n", buf);
  
  // TODO: remove repl?
  nfct_to_pkt_info(&orig, &reply, ct);
  //printf("[ME] port=22 %s\n", new_buf);
  //pktinfo2str(new_buf, &orig, 4096);
  //printf("[NF] %s\n", buf);
  if (orig.packet_count > 0 || orig.payload_size > 0) {
      node_cache_add_pkt_info(cb_data->node_cache, &orig, now);
      //node_cache_add_pkt_info(cb_data->node_cache, &reply, now);
      flow_list_add_pktinfo(cb_data->flow_list, &orig);
      //flow_list_add_pktinfo(cb_data->flow_list, &reply);
      //printf("[XX] add pkt infos to flow list, tree size now %d\n", tree_size(cb_data->flow_list->flows));
  }
  //node_cache_add_pkt_info(cb_data->node_cache, &reply, now);
  //do_flow_list(cb_data->flow_list, now, &orig);
  //do_flow_list(cb_data->flow_list, now, &reply);

  nfct_destroy(ct);

  return MNL_CB_OK;
}

/* note, this opens a socket every call (i.e. every second)
 * we may want to put that outside of the main loop and add it to the poll thing
 * Then again, now we can take our time without worrying about the socket buffer
 * filling
 */
int do_read(cb_data_t* cb_data) {
  struct mnl_socket *nl;
  struct nlmsghdr *nlh;
  struct nfgenmsg *nfh;
  char buf[MNL_SOCKET_BUFFER_SIZE];
  unsigned int seq, portid;
  int ret;
  int i = 0;

  nl = mnl_socket_open(NETLINK_NETFILTER);
  if (nl == NULL) {
    perror("mnl_socket_open");
    exit(EXIT_FAILURE);
  }

  if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
    perror("mnl_socket_bind");
    exit(EXIT_FAILURE);
  }
  portid = mnl_socket_get_portid(nl);

  nlh = mnl_nlmsg_put_header(buf);
  nlh->nlmsg_type = (NFNL_SUBSYS_CTNETLINK << 8) | IPCTNL_MSG_CT_GET_CTRZERO;
  nlh->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
  nlh->nlmsg_seq = seq = time(NULL);

  nfh = mnl_nlmsg_put_extra_header(nlh, sizeof(struct nfgenmsg));
  nfh->nfgen_family = AF_INET;
  nfh->version = NFNETLINK_V0;
  nfh->res_id = 0;

  ret = mnl_socket_sendto(nl, nlh, nlh->nlmsg_len);
  if (ret == -1) {
    perror("mnl_socket_recvfrom");
    exit(EXIT_FAILURE);
  }

  ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
  while (ret > 0) {
    ret = mnl_cb_run(buf, ret, seq, portid, conntrack_cb, cb_data);
    if (ret <= MNL_CB_STOP)
      break;
    ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
  }
  if (ret == -1) {
    perror("mnl_socket_recvfrom");
    exit(EXIT_FAILURE);
  }

  mnl_socket_close(nl);

  return 0;
}

void handle_json_command(const char* data) {
  // TODO: port json commands
}

void on_mqtt_message(struct mosquitto* mosq, void* user_data, const struct mosquitto_message* msg) {
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

    mosquitto_message_callback_set(mosq, on_mqtt_message);
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

void init_mosquitto(const char* host, int port) {
    mosquitto_lib_init();

    printf("[XX] connecting to mosquitto server at %s:%d\n", host, port);
    connect_mosquitto(host, port);
    printf("[XX] connected\n");

    send_command_restart();
    //handle_command_get_list(SPIN_CMD_GET_IGNORE, "filters");
    //handle_command_get_list(SPIN_CMD_GET_BLOCK, "blocks");
    //handle_command_get_list(SPIN_CMD_GET_EXCEPT, "alloweds");
    printf("[XX] restart command sent\n");
}

void init_cache() {
    dns_cache = dns_cache_create();
    node_cache = node_cache_create();
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

/* ----------- */
/* DNS Capture */
/* ----------- */

u_int32_t treat_pkt(struct nfq_data *nfa, int* verdict) {
    (void)nfa;
    *verdict = 1;
    printf("[XX] GOT PKT TO TREAT\n");
    return 1;
}

/* Definition of callback function */
static int dns_cap_cb(struct nfq_q_handle *qh, struct nfgenmsg *nfmsg,
              struct nfq_data *nfa, void *data)
{
    int verdict;
    u_int32_t id = treat_pkt(nfa, &verdict); /* Treat packet */
    return nfq_set_verdict(qh, id, verdict, 0, NULL); /* Verdict packet */
}
 

/* Set callback function */
/*
for (;;) {
}
*/

/* ------------------ */
/* end of DNS Capture */
/* ------------------ */

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
    printf("-d\t\t\tlog debug messages (set log level to LOG_DEBUG)\n");
    printf("-h\t\t\tshow this help\n");
    printf("-l\t\t\trun in local mode (do not check for ARP cache entries)\n");
    printf("-o\t\t\tlog to stdout instead of syslog\n");
    printf("-m <address>\t\t\tHostname or IP address of the MQTT server\n");
    printf("-p <port number>\t\t\tPort number of the MQTT server\n");
    printf("-v\t\t\tprint the version of spind and exit\n");
}

int main_loop() {
    ssize_t c = 0;
    int rs;
    message_type_t type;
    struct timeval tv;
    struct pollfd fds[1];
    uint32_t now, last_mosq_poll;

    cb_data_t* cb_data = (cb_data_t*)malloc(sizeof(cb_data_t));

    buffer_t* json_buf = buffer_create(4096);
    buffer_allow_resize(json_buf);
    int mosq_result;

    tv.tv_sec = 0;
    tv.tv_usec = 500;

    // set all fds. currently only one
    fds[0].fd = mosquitto_socket(mosq);
    fds[0].events = POLLIN;

    now = time(NULL);
    last_mosq_poll = now;
    flow_list_t* flow_list = flow_list_create(time(NULL));

    //char str[1024];
    size_t pos = 0;


    cb_data->flow_list = flow_list;
    cb_data->node_cache = node_cache;

    /* specific queue for DNS traffic */
    //int qh = nfq_create_queue(h,  0, &dns_cap_cb, NULL);

    /* Read message from kernel or mqtt */
    while (running) {

        /* check DNS traffic queue */
        /*
        if ((rv = recv(fd, buf, sizeof(buf), 0)) >= 0) {
            nfq_handle_packet(h, buf, rv); // send packet to callback
            continue;
        }*/

        do_read(cb_data);
        if (flow_list_should_send(flow_list, now)) {
            //printf("[XX] should send flow list A (total size %d)\n", tree_size(flow_list->flows));
            if (!flow_list_empty(flow_list)) {
                // create json, send it
                buffer_reset(json_buf);
                create_traffic_command(node_cache, flow_list, json_buf, now);
                if (buffer_finish(json_buf)) {
                    //printf("[XX] SENDING: %s\n", buffer_str(json_buf));
                    mosq_result = mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
                } else {
                    printf("[XX] unable to finish buffer\n");
                }
            } else {
                //printf("[XX] but it is empty\n");
            }
            flow_list_clear(flow_list, now);
        }


        // check mqtt fd as well

        rs = poll(fds, 1, 1000);
        now = time(NULL);

        if (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME / 2) {
            //spin_log(LOG_DEBUG, "Calling loop for keepalive check\n");
            mosquitto_loop_misc(mosq);
            last_mosq_poll = now;
        }

        if (rs < 0) {
            spin_log(LOG_ERR, "error in poll(): %s\n", strerror(errno));
        } else if (rs == 0) {
            // do nothing further
        } else {
/*
            if (1) {
                printf("[XX] CALL do_read()\n");
                
                do_read(cb_data);
                printf("[XX] DONE do_read()\n");
*/
/*
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
*/
/*
            }
*/
            printf("[XX] check mosq\n");
            if ((fds[0].revents & POLLIN) || (now - last_mosq_poll >= MOSQUITTO_KEEPALIVE_TIME)) {
                printf("[XX] have some message on mosq\n");
                //spin_log(LOG_DEBUG, "Calling loop for data\n");
                mosquitto_loop(mosq, 0, 10);
                last_mosq_poll = now;
            } else if (fds[0].revents) {
                printf("[XX] have bad message on mosq\n");
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
        //printf("[XX] end of loop body\n");

        sleep(1);
    }
    flow_list_destroy(flow_list);
    buffer_destroy(json_buf);
    free(cb_data);
    return 0;
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
    printf("[XX] A\n");
    init_mosquitto(mosq_host, mosq_port);
    printf("[XX] B\n");
    signal(SIGINT, int_handler);
    printf("[XX] C\n");
    fflush(stdout);

    running = 1;
    //result = init_netlink();
    // main loop goes here
    main_loop();

    cleanup_cache();

    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    return 0;
}


