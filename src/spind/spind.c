#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <assert.h>

#include "core2block.h"
#include "core2conntrack.h"
#include "core2extsrc.h"
#include "core2nflog_dns.h"
#include "core2pubsub.h"
#include "dnshooks.h"
#include "extsrc.h"
#include "ipl.h"
#include "mainloop.h"
#include "nflogroutines.h"
#include "nfqroutines.h"
#include "rpc_calls.h"
#include "rpc_json.h"
#include "spinconfig.h"
#include "spinhook.h"
#include "spin_log.h"
#include "statistics.h"
#include "version.h"
#include "config.h"

node_cache_t* node_cache;
dns_cache_t* dns_cache;

static int local_mode;

const char* config_file = NULL;
const char* mosq_host;
int mosq_port;

STAT_MODULE(spind)




/*
 * End of node info store code
 */

static void
send_command_node_info(int nodenum, spin_data sd) {
    char mosqchan[100];
    spin_data command;

    command = spin_data_create_mqtt_command("nodeInfo", NULL, sd);

    sprintf(mosqchan, "SPIN/traffic/node/%d", nodenum);
    core2pubsub_publish_chan(mosqchan, command, 1);

    spin_data_delete(command);
}

static void
update_node_ips(int nodenum, tree_t *tree) {
    tree_entry_t* ip_entry;

    ip_entry = tree_first(tree);
    while (ip_entry != NULL) {
        c2b_node_ipaddress(nodenum, ip_entry->key);
        ip_entry = tree_next(ip_entry);
    }
}

// TODO: move to lib?

#define NODE_FILENAME_DIR "/etc/spin/nodestore"
#define NODEPAIRFILE "/etc/spin/nodepair.list"
static char node_filename[100];
static char *
node_filename_int(int nodenum) {
    sprintf(node_filename, "%s/%d", NODE_FILENAME_DIR, nodenum);
    return node_filename;
}
static void
store_node_info(int nodenum, spin_data sd) {
    FILE *nodefile;
    char *sdstr;

    mkdir(NODE_FILENAME_DIR, 0777);
    nodefile = fopen(node_filename_int(nodenum), "w");
    sdstr = spin_data_serialize(sd);
    fprintf(nodefile, "%s\n", sdstr);
    spin_data_ser_delete(sdstr);
    fclose(nodefile);
}

static void
node_is_updated(node_t *node) {
    spin_data sd;

    sd = spin_data_node(node);
    if (node->persistent) /* Persistent? */ {
        // Store modified node in file
        store_node_info(node->id, sd);

        // Update IP addresses in c2b
        update_node_ips(node->id, node->ips);
    }
    // This effectively deletes sd
    send_command_node_info(node->id, sd);
}

void
publish_nodes() {

    // Currently called just before traffic messages
    // Maybe also with timer?
    //
    // If so, how often?

    node_callback_new(node_cache, node_is_updated);
}

void send_command_blocked(pkt_info_t* pkt_info) {
    spin_data pkt_sd, cmd_sd;

    // Publish recently changed nodes
    publish_nodes();

    pkt_sd = spin_data_pkt_info(node_cache, pkt_info);
    cmd_sd = spin_data_create_mqtt_command("blocked", NULL, pkt_sd);

    core2pubsub_publish_chan(NULL, cmd_sd, 0);

    spin_data_delete(cmd_sd);
}

void send_command_dnsquery(dns_pkt_info_t* pkt_info) {
    spin_data dns_sd, cmd_sd;

    // Publish recently changed nodes
    publish_nodes();

    dns_sd = spin_data_dns_query_pkt_info(node_cache, pkt_info);
    cmd_sd = spin_data_create_mqtt_command("dnsquery", NULL, dns_sd);

    core2pubsub_publish_chan(NULL, cmd_sd, 0);

    spin_data_delete(cmd_sd);
}

// function definition below
// void connect_mosquitto(const char* host, int port);
void maybe_sendflow(flow_list_t *flow_list, time_t now) {
    STAT_COUNTER(ctr1, send-flow, STAT_TOTAL);
    STAT_COUNTER(ctr2, create-traffic, STAT_TOTAL);

    if (flow_list_should_send(flow_list, now)) {
        STAT_VALUE(ctr1, 1);
        if (!flow_list_empty(flow_list)) {
            spin_data cmd_sd;

            // Publish recently changed nodes
            publish_nodes();

            // create json, send it
            STAT_VALUE(ctr2, 1);
            cmd_sd = spin_data_create_traffic(node_cache, flow_list, now);
            core2pubsub_publish_chan(NULL, cmd_sd, 0);

            spin_data_delete(cmd_sd);

        }
        flow_list_clear(flow_list, now);
    }
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

/* Worker function to regularly synchronize the ip lists
 * (see spin_list.h) to persistent storage
 */
void wf_ipl(void *arg, int data, int timeout) {
    int i;
    struct list_info *lip;
    char *fname;
    struct list_info* ipl_list_ar = get_spin_iplists();

    if (timeout) {
        // What else could it be ??
        for (i=0; i<N_IPLIST; i++) {
            lip = &ipl_list_ar[i];
            if (lip->li_modified) {
                fname = ipl_filename(lip);
                store_ip_tree(lip->li_tree, fname);
                lip->li_modified = 0;
            }
        }
    }
}

void
init_ipl_list_ar() {
    init_all_ipl(get_spin_iplists());

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
    printf("-c <file>\tspecify spind config file (default: %s)\n", CONFIG_FILE);
    printf("-d\t\t\tlog debug messages (set log level to LOG_DEBUG)\n");
    printf("-e <path>\t\textsrc socket path (default: %s)\n", EXTSRC_SOCKET_PATH);
    printf("-h\t\t\tshow this help\n");
    printf("-j <path>\t\tJSON RPC socket path (default: %s)\n", JSON_RPC_SOCKET_PATH);
    printf("-l\t\t\trun in local mode (do not check for ARP cache entries)\n");
    printf("-m <address>\t\tHostname or IP address of the MQTT server\n");
    printf("-o\t\t\tlog to stdout instead of syslog\n");
    printf("-p <port number>\tPort number of the MQTT server\n");
    printf("-v\t\t\tprint the version of spind and exit\n");
}

// Worker function to cleanup cache, gets called regularly
void node_cache_clean_wf() {
    // should we make this configurable?
    const uint32_t node_cache_retain_seconds = spinconfig_node_cache_retain_time();
    static int runcounter;
    uint32_t older_than = time(NULL) - node_cache_retain_seconds;

    spinhook_clean(node_cache);
    runcounter++;
    if (runcounter > 3) {
        node_cache_clean(node_cache, older_than);
        runcounter = 0;
    }
}

#define CLEAN_TIMEOUT 15000

void init_cache() {
    dns_cache = dns_cache_create();
    node_cache = node_cache_create();

    mainloop_register("node_cache_clean", node_cache_clean_wf, (void *) 0, 0, CLEAN_TIMEOUT);
}

void cleanup_cache() {
    dns_cache_destroy(dns_cache);
    node_cache_destroy(node_cache);
}

void ubus_main();

int main(int argc, char** argv) {
    int c;
    int log_verbosity = 1;
    int use_syslog;
    int debug_mode = 0;
    int cmdline_console_output = 0;
    char *extsrc_socket_path = EXTSRC_SOCKET_PATH;
#ifndef USE_UBUS
    char *json_rpc_socket_path = JSON_RPC_SOCKET_PATH;
#endif

    while ((c = getopt (argc, argv, "c:de:hj:lm:op:v")) != -1) {
        switch (c) {
        case 'c':
            config_file = optarg;
            break;
        case 'd':
            // Set up logging directly, do not wait until we read config
            log_verbosity = 7;
            debug_mode = 1;
            spin_log_init(use_syslog, log_verbosity, "spind");
            break;
        case 'e':
            extsrc_socket_path = optarg;
            break;
        case 'h':
            print_help();
            exit(0);
            break;
        case 'j':
#ifdef USE_UBUS
            fprintf(stderr, "Error: this build of SPIN does not use JSON RPC.\n");
            fprintf(stderr, "Cannot specify -j.\n");
            exit(1);
#else
            json_rpc_socket_path = optarg;
#endif
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
            cmdline_console_output = 1;
            // Again, set up logging directly, so even config reading goes to
            // console
            if (!debug_mode) {
                log_verbosity = 1;
            }
            spin_log_init(0, log_verbosity, "spind");
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
            print_help();
            exit(1);
        }
    }

    if (config_file) {
        init_config(config_file, 1);
    } else {
        // Don't error if default config file doesn't exist
        init_config(CONFIG_FILE, 0);
    }
    if (!cmdline_console_output) {
        use_syslog = spinconfig_log_usesyslog();
    }
    if (!debug_mode) {
        log_verbosity = spinconfig_log_loglevel();
        spin_log_init(use_syslog, log_verbosity, "spind");
    }

    mosq_host = spinconfig_pubsub_host();
    mosq_port = spinconfig_pubsub_port();

    log_version();

    SPIN_STAT_START();

    init_cache();

    dns_hooks_init(node_cache, dns_cache);
    init_core2conntrack(node_cache, local_mode, spinhook_traffic);
    init_core2nflog_dns(node_cache, dns_cache);

    init_core2block();

    init_core2extsrc(node_cache, dns_cache, extsrc_socket_path);

    init_ipl_list_ar();

    init_rpcs(node_cache);

    init_mosquitto(mosq_host, mosq_port);
    signal(SIGINT, int_handler);

#ifdef USE_UBUS
    ubus_main();
#else
    init_json_rpc(json_rpc_socket_path);
#endif

    mainloop_run();

    cleanup_cache();
    cleanup_core2block();
    cleanup_core2conntrack();
    cleanup_core2nflog_dns();
    cleanup_core2extsrc();

    nfq_close_handle();
    nflog_close_handle();
    rpc_cleanup();

    finish_mosquitto();
    clean_all_ipl();

    SPIN_STAT_FINISH();

    return 0;
}
