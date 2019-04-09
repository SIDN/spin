#include <sys/socket.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <assert.h>

#include "spinconfig.h"
#include "pkt_info.h"
#include "util.h"
#include "ipl.h"
#include "spin_list.h"
#include "node_cache.h"
#include "dns_cache.h"
#include "tree.h"
#include "spin_log.h"
#include "core2pubsub.h"
#include "core2block.h"
#include "core2nflog_dns.h"
#include "core2conntrack.h"

#include "handle_command.h"
#include "mainloop.h"
#include "statistics.h"
#include "version.h"

node_cache_t* node_cache;
dns_cache_t* dns_cache;

static int local_mode;

const char* mosq_host;
int mosq_port;
int stop_on_error;

STAT_MODULE(spind)

#define JSONBUFSIZ      4096

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
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    buffer_t* pkt_json = buffer_create(JSONBUFSIZ);
    unsigned int p_size;

    p_size = pkt_info2json(node_cache, pkt_info, pkt_json);
    if (p_size > 0) {
        buffer_finish(pkt_json);
        create_mqtt_command(response_json, "blocked", NULL, buffer_str(pkt_json));
        if (buffer_finish(response_json)) {
            core2pubsub_publish(response_json);
        } else {
            spin_log(LOG_WARNING, "Error converting blocked pkt_info to JSON; partial packet: %s\n", buffer_str(response_json));
        }
    } else {
        spin_log(LOG_DEBUG, "[XX] did not get an actual block command (size 0)\n");
    }
    buffer_destroy(response_json);
    buffer_destroy(pkt_json);
}

void send_command_dnsquery(dns_pkt_info_t* pkt_info) {
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    buffer_t* pkt_json = buffer_create(JSONBUFSIZ);
    unsigned int p_size;
    STAT_COUNTER(ctr, dnsquerysize, STAT_MAX);

    p_size = dns_query_pkt_info2json(node_cache, pkt_info, pkt_json);
    if (p_size > 0) {
        spin_log(LOG_DEBUG, "[XX] got an actual dns query command (size >0)\n");
        buffer_finish(pkt_json);

        STAT_VALUE(ctr, strlen(buffer_str(pkt_json)));

        create_mqtt_command(response_json, "dnsquery", NULL, buffer_str(pkt_json));
        if (buffer_finish(response_json)) {
            core2pubsub_publish(response_json);
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
// void connect_mosquitto(const char* host, int port);

void maybe_sendflow(flow_list_t *flow_list, time_t now) {
    buffer_t* json_buf = buffer_create(JSONBUFSIZ);
    buffer_allow_resize(json_buf);
    STAT_COUNTER(ctr1, send-flow, STAT_TOTAL);
    STAT_COUNTER(ctr2, create-traffic, STAT_TOTAL);

    if (flow_list_should_send(flow_list, now)) {
        STAT_VALUE(ctr1, 1);
        if (!flow_list_empty(flow_list)) {
            // create json, send it
            buffer_reset(json_buf);
            STAT_VALUE(ctr2, 1);
            create_traffic_command(node_cache, flow_list, json_buf, now);
            if (buffer_finish(json_buf)) {
                core2pubsub_publish(json_buf);
                // mosq_result = mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
            }
        }
        flow_list_clear(flow_list, now);
    }
    buffer_destroy(json_buf);
}

void
report_block(int af, int proto, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port, int payloadsize) {
    pkt_info_t pkt;

    pkt.family = af;
    pkt.protocol = proto;
    memcpy(pkt.src_addr, src_addr, 16);
    memcpy(pkt.dest_addr, dest_addr, 16);
    pkt.src_port = src_port;
    pkt.dest_port = dest_port;
    pkt.payload_size = payloadsize;
    pkt.packet_count = 1;

    send_command_blocked(&pkt);
}

/*
 * The three lists of IP addresses are now kept in memory in spind, with
 * a copy written to file.
 * BLOCK, IGNORE, ALLOW
 */
struct list_info ipl_list_ar[N_IPLIST] = {
    { 0, "/etc/spin/block.list",   0 },
    { 0, "/etc/spin/ignore.list",  0 },
    { 0, "/etc/spin/allow.list",   0 },
};

void wf_ipl(void *arg, int data, int timeout) {
    int i;
    struct list_info *lip;

    if (timeout) {
        // What else could it be ??
        for (i=0; i<N_IPLIST; i++) {
            lip = &ipl_list_ar[i];
            if (lip->li_modified) {
                store_ip_tree(lip->li_tree, lip->li_filename);
                lip->li_modified = 0;
            }
        }
    }
}


void
init_ipl_list_ar() {
    init_all_ipl(ipl_list_ar);

    // Sync trees to files every 2.5 seconds for now
    mainloop_register("IP list sync", wf_ipl, (void *) 0, 0, 2500);
}

#define MAXSR 3 /* More than this would be excessive */
static
struct sreg {
    char *         sr_name;            /* Name of module for debugging */
    spinfunc       sr_wf;              /* The to-be-called work function */
    void *         sr_wfarg;           /* Call back argument */
    int            sr_list[N_IPLIST];  /* Which lists to subscribe to */
} sr[MAXSR];

static int n_sr = 0;
void spin_register(char *name, spinfunc wf, void *arg, int list[N_IPLIST]) {
    int i;

    spin_log(LOG_DEBUG, "Spind registered %s(..., (%d,%d,%d))\n", name, list[0], list[1], list[2]);
    assert(n_sr < MAXSR);
    sr[n_sr].sr_name = name;
    sr[n_sr].sr_wf = wf;
    sr[n_sr].sr_wfarg = arg;
    for (i=0;i<N_IPLIST;i++) {
        sr[n_sr].sr_list[i] = list[i];
    }
    n_sr++;
}

static void
list_inout_do_ip(int iplist, int addrem, ip_t *ip_addr) {
    int i;

    for (i = 0; i < n_sr; i++) {
        if (sr[i].sr_list[iplist]) {
            // This one is interested
            spin_log(LOG_DEBUG, "Called spin list func %s(%d,%d)\n",
                                sr[i].sr_name, iplist, addrem);
            (*sr[i].sr_wf)(sr[i].sr_wfarg, iplist, addrem, ip_addr);
        }
    }
}

static void
call_kernel_for_tree(int iplist, int addrem, tree_t *tree) {
    tree_entry_t* ip_entry;

    ip_entry = tree_first(tree);
    while (ip_entry != NULL) {
        // spin_log(LOG_DEBUG, "ckft: %d %x\n", cmd, tree);
        list_inout_do_ip(iplist, addrem, ip_entry->key);
        ip_entry = tree_next(ip_entry);
    }
}

static void
push_ips_from_list_to_kernel(int iplist) {

    //
    // Make sure the kernel gets to know on init
    //
    call_kernel_for_tree(iplist, SF_ADD, ipl_list_ar[iplist].li_tree);
}

void push_all_ipl() {
    int i;

    for (i=0; i<N_IPLIST; i++) {
        // Push into kernel
        push_ips_from_list_to_kernel(i);
    }
}

static void
call_kernel_for_node_ips(int listid, int addrem, node_t *node) {

    if (node == NULL) {
        return;
    }
    call_kernel_for_tree(listid, addrem, node->ips);
}

void handle_command_remove_ip_from_list(int iplist, ip_t* ip) {

    list_inout_do_ip(iplist, SF_REM, ip);
    remove_ip_from_li(ip, &ipl_list_ar[iplist]);
}

void handle_command_remove_all_from_list(int iplist) {
    tree_entry_t* ip_entry;

    ip_entry = tree_first(ipl_list_ar[iplist].li_tree);
    while (ip_entry != NULL) {
        list_inout_do_ip(iplist, SF_REM, ip_entry->key);
        ip_entry = tree_next(ip_entry);
    }
    // Remove whole tree, will be recreated
    tree_destroy(ipl_list_ar[iplist].li_tree);
}

static node_t *
find_node_id(int node_id) {
    node_t *node;

    /*
     * Find it and give warning if non-existent. This should not happen.
     */
    node = node_cache_find_by_id(node_cache, node_id);
    if (node == NULL) {
        spin_log(LOG_WARNING, "Node-id %d not found!\n", node_id);
        return NULL;
    }
    return node;
}

// Switch code
void handle_list_membership(int listid, int addrem, int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
        return;

    node->is_onlist[listid] = addrem == SF_ADD ? 1 : 0;

    call_kernel_for_node_ips(listid, addrem, node);
    if (addrem == SF_ADD) {
        add_ip_tree_to_li(node->ips, &ipl_list_ar[listid]);
    } else {
        remove_ip_tree_from_li(node->ips, &ipl_list_ar[listid]);
    }
}

void handle_command_reset_ignores() {

    // clear the ignores; derive them from our own addresses again

    // First remove all current ignores
    handle_command_remove_all_from_list(IPLIST_IGNORE);

    // Now generate new list
    system("/usr/lib/spin/show_ips.lua -o /etc/spin/ignore.list -f");

    // Load the ignores again
    init_ipl(&ipl_list_ar[IPLIST_IGNORE]);
    push_ips_from_list_to_kernel(IPLIST_IGNORE);
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

static void
iptree2json(tree_t* tree, buffer_t* result) {
    tree_entry_t* cur;
    char ip_str[INET6_ADDRSTRLEN];
    char *prefix;

    cur = tree_first(tree);

    if (cur == NULL) {
        // empty tree
        buffer_write(result, " [ ] ");
        return;
    }

    // Prefix is [ at first, after that ,
    prefix = " [ ";

    while (cur != NULL) {
        spin_ntop(ip_str, cur->key, INET6_ADDRSTRLEN);
        buffer_write(result, prefix);
        buffer_write(result, "\"%s\" ", ip_str);
        prefix = " , ";
        cur = tree_next(cur);
    }
    buffer_write(result, " ] ");
}

void handle_command_get_iplist(int iplist, const char* json_command) {
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    buffer_t* result_json = buffer_create(JSONBUFSIZ);

    iptree2json(ipl_list_ar[iplist].li_tree, result_json);
    if (!buffer_ok(result_json)) {
        buffer_destroy(result_json);
        buffer_destroy(response_json);
        return;
    }
    buffer_finish(result_json);

    spin_log(LOG_DEBUG, "get_iplist result %s\n", buffer_str(result_json));

    create_mqtt_command(response_json, json_command, NULL, buffer_str(result_json));
    if (!buffer_ok(response_json)) {
        buffer_destroy(result_json);
        buffer_destroy(response_json);
        return;
    }
    buffer_finish(response_json);

    core2pubsub_publish(response_json);

    buffer_destroy(result_json);
    buffer_destroy(response_json);

}

void int_handler(int signal) {

    mainloop_end();
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

// Worker function to cleanup cache, gets called regularly
void node_cache_clean_wf() {
    // should we make this configurable?
    const uint32_t node_cache_retain_seconds = spinconfig_node_cache_retain_time();
    uint32_t older_than = time(NULL) - node_cache_retain_seconds;
    node_cache_clean(node_cache, older_than);
}

void init_cache() {
    dns_cache = dns_cache_create();
    node_cache = node_cache_create();
    mainloop_register("node_cache_clean", node_cache_clean_wf, (void *) 0, 0, 60000);
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

int main(int argc, char** argv) {
    int c;
    int log_verbosity;
    int use_syslog;

    init_config();
    log_verbosity = spinconfig_log_loglevel();
    use_syslog = spinconfig_log_usesyslog();

    mosq_host = spinconfig_pubsub_host();
    mosq_port = spinconfig_pubsub_port();
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

    init_core2conntrack(node_cache, local_mode);
    init_core2nflog_dns(node_cache, dns_cache);

    init_core2block();

    init_ipl_list_ar();

    init_mosquitto(mosq_host, mosq_port);
    signal(SIGINT, int_handler);

    push_all_ipl();

    mainloop_run();

    cleanup_cache();
    cleanup_core2block();

    finish_mosquitto();

    return 0;
}
