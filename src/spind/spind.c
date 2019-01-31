#include <sys/socket.h>
#include <sys/types.h>
#include <linux/netlink.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "pkt_info.h"
#include "node_cache.h"
#include "tree.h"
#include "netlink_commands.h"
#include "spin_log.h"
#include "core2pubsub.h"
#include "core2kernel.h"
#include "core2block.h"

// perhaps remove
#include "spin_cfg.h"

#include "handle_command.h"
#include "mainloop.h"
#include "version.h"

node_cache_t* node_cache;

static int local_mode;

const char* mosq_host;
int mosq_port;
int stop_on_error;

/*
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
*/

#ifdef notdef
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
#endif

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
        core2pubsub_publish(response_json);
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
    buffer_t* json_buf = buffer_create(4096);
    buffer_allow_resize(json_buf);
    int mosq_result;

    if (flow_list_should_send(flow_list, now)) {
	if (!flow_list_empty(flow_list)) {
	    // create json, send it
	    buffer_reset(json_buf);
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

#ifdef notdef
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
#endif

/*
 * The three lists of IP addresses are now kept in memory in spind, with
 * a copy written to file.
 * BLOCK, FILTER, ALLOW
 * The lists in the kernel module will not be read back, even while there is a
 * kernel module.
 */

#define N_IPLISTS	3

struct list_info {
    tree_t *	li_tree;		// Tree of IP addresses
    char *	li_filename;		// Name of shadow file
    int		li_modified;		// File should be written
} ipl_list_ar[N_IPLISTS] = {
	{	0,	"/etc/spin/block.list",		0	},
	{	0,	"/etc/spin/ignore.list",	0	},
	{	0,	"/etc/spin/allow.list",		0	},
};

#define ipl_block ipl_list_ar[IPLIST_BLOCK]
#define ipl_ignore ipl_list_ar[IPLIST_IGNORE]
#define ipl_allow ipl_list_ar[IPLIST_ALLOW]

void wf_ipl(void *arg, int data, int timeout) {
    int i;
    struct list_info *lip;

    if (timeout) {
	// What else could it be ??
	for (i=0; i<N_IPLISTS; i++) {
	    lip = &ipl_list_ar[i];
	    if (lip->li_modified) {
		store_ip_tree(lip->li_tree, lip->li_filename);
		lip->li_modified = 0;
	    }
	}
    }
}

void init_all_ipl() {
    int i;
    struct list_info *lip;
    int cnt;

    for (i=0; i<N_IPLISTS; i++) {
	lip = &ipl_list_ar[i];
	lip->li_tree = tree_create(cmp_ips);
	cnt = read_ip_tree(lip->li_tree, lip->li_filename);
	spin_log(LOG_DEBUG, "File %s, read %d entries\n", lip->li_filename, cnt);

	// Push into kernel
	push_ips_from_list_to_kernel(i);
    }

    // Sync trees to files every 2.5 seconds for now
    mainloop_register("IP list sync", wf_ipl, (void *) 0, 0, 2500);
}

#ifdef notdef
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
#endif

static void
add_ip_tree_to_li(tree_t* tree, struct list_info *lip) {
    tree_entry_t* cur;

    if (tree == NULL)
	return;
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_add(lip->li_tree, cur->key_size, cur->key, cur->data_size, cur->data, 1);
        cur = tree_next(cur);
    }
    lip->li_modified++;
}

static void
remove_ip_tree_from_li(tree_t *tree, struct list_info *lip) {
    tree_entry_t* cur;

    if (tree == NULL)
	return;
    cur = tree_first(tree);
    while(cur != NULL) {
        tree_remove(lip->li_tree, cur->key_size, cur->key);
        cur = tree_next(cur);
    }
    lip->li_modified++;
}

void remove_ip_from_li(ip_t* ip, struct list_info *lip) {

    tree_remove(lip->li_tree, sizeof(ip_t), ip);
    lip->li_modified++;
}

#ifdef notdef
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
#endif

static
call_kernel_for_tree(config_command_t cmd, tree_t *tree) {
    tree_entry_t* ip_entry;

    ip_entry = tree_first(tree);
    while (ip_entry != NULL) {
	core2kernel_do_ip(cmd, ip_entry->key);
        ip_entry = tree_next(ip_entry);
    }
}

push_ips_from_list_to_kernel(int iplist) {
    static config_command_t addip_cmds[] = {
	SPIN_CMD_ADD_BLOCK,
	SPIN_CMD_ADD_IGNORE,
	SPIN_CMD_ADD_EXCEPT
    };

    //
    // Make sure the kernel gets to know on init
    //
    call_kernel_for_tree(addip_cmds[iplist], ipl_list_ar[iplist].li_tree);
}

static void
call_kernel_for_node_ips(config_command_t cmd, node_t *node) {

    if (node == NULL) {
        return;
    }
    call_kernel_for_tree(cmd, node->ips);
}

#ifdef notdef
void handle_command_remove_ip(config_command_t cmd, ip_t* ip) {

    core2kernel_do_ip(cmd, ip);

    switch(cmd) {
    case SPIN_CMD_REMOVE_BLOCK:
	remove_ip_from_li(ip, &ipl_block);
	break;
    case SPIN_CMD_REMOVE_IGNORE:
	remove_ip_from_li(ip, &ipl_ignore);
	break;
    case SPIN_CMD_REMOVE_EXCEPT:
	remove_ip_from_li(ip, &ipl_allow);
	break;
    }
}
#endif

void handle_command_remove_ip_from_list(int iplist, ip_t* ip) {
    static config_command_t rmip_cmds[] = {
	SPIN_CMD_REMOVE_BLOCK,
	SPIN_CMD_REMOVE_IGNORE,
	SPIN_CMD_REMOVE_EXCEPT
    };

    core2kernel_do_ip(rmip_cmds[iplist], ip);
    remove_ip_from_li(ip, &ipl_list_ar[iplist]);
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

void handle_command_block_data(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    // the is_blocked status is only read if this node had a new ip address added, so update it now
    node->is_blocked = 1;

    call_kernel_for_node_ips(SPIN_CMD_ADD_BLOCK, node);
    add_ip_tree_to_li(node->ips, &ipl_block);
    // add_ip_tree_to_file(ips, "/etc/spin/block.list");
}

void handle_command_stop_block_data(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    // the is_blocked status is only read if this node had a new ip address added, so update it now
    node->is_blocked = 0;

    call_kernel_for_node_ips(SPIN_CMD_REMOVE_BLOCK, node);
    remove_ip_tree_from_li(node->ips, &ipl_block);
    // remove_ip_tree_from_file(ips, "/etc/spin/block.list");
}

void handle_command_add_ignore(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    call_kernel_for_node_ips(SPIN_CMD_ADD_IGNORE, node);
    add_ip_tree_to_li(node->ips, &ipl_ignore);
    // add_ip_tree_to_file(ips, "/etc/spin/ignore.list");
}

void handle_command_remove_ignore(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    call_kernel_for_node_ips(SPIN_CMD_REMOVE_IGNORE, node);
    remove_ip_tree_from_li(node->ips, &ipl_ignore);
    // remove_ip_tree_from_file(ips, "/etc/spin/ignore.list");
}

void handle_command_allow_data(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    // the is_allowed status is only read if this node had a new ip address added, so update it now
    node->is_allowed = 1;

    call_kernel_for_node_ips(SPIN_CMD_ADD_EXCEPT, node);
    add_ip_tree_to_li(node->ips, &ipl_allow);
    // add_ip_tree_to_file(ips, "/etc/spin/allow.list");
}

void handle_command_stop_allow_data(int node_id) {
    node_t* node;

    if ((node = find_node_id(node_id)) == NULL)
    	return;

    // the is_allowed status is only read if this node had a new ip address added, so update it now
    node->is_allowed = 0;

    call_kernel_for_node_ips(SPIN_CMD_REMOVE_EXCEPT, node);
    remove_ip_tree_from_li(node->ips, &ipl_allow);
    // remove_ip_tree_from_file(ips, "/etc/spin/allow.list");
}

void handle_command_reset_ignores() {
    // clear the ignores; derive them from our own addresses again
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

static void
iptree2json(tree_t* tree, buffer_t* result) {
    tree_entry_t* cur;
    ip_t *ip_addr;
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
    buffer_t* response_json = buffer_create(4096);
    buffer_t* result_json = buffer_create(4096);
    unsigned int response_size;

    iptree2json(ipl_list_ar[iplist].li_tree, result_json);
    if (!buffer_ok(result_json)) {
        buffer_destroy(result_json);
        buffer_destroy(response_json);
        return;
    }
    buffer_finish(result_json);

    fprintf(stderr, "[XX] get_iplist result %s\n", buffer_str(result_json));

    response_size = create_mqtt_command(response_json, json_command, NULL, buffer_str(result_json));
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

#ifdef notdef
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
    // pubsub_publish(response_size, buffer_str(response_json));
    core2pubsub_publish(response_json);

    netlink_command_result_destroy(command_result);
    buffer_destroy(result_json);
    buffer_destroy(response_json);
}
#endif

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

    init_all_ipl();

    init_mosquitto(mosq_host, mosq_port);
    signal(SIGINT, int_handler);

    result = init_netlink(local_mode);

    // init_core2block();

    mainloop_run();

    cleanup_cache();
    cleanup_netlink();

    // cleanup_core2block();

    finish_mosquitto();

    return 0;
}
