#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
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

int omitnode;

STAT_MODULE(spind)

/*
 *
 * Code to store and retrieve node info from files for persistency
 *
 */

#define CJS
#ifdef CJS

#include "cJSON.h"

static node_t*
decode_node_info(char *data, int datalen) {
    cJSON *node_json;
    node_t *newnode;
    cJSON *id_json, *mac_json, *name_json, *ips_json, *ip_json, *domains_json, *domain_json;

    newnode = 0;

    node_json = cJSON_Parse(data);
    if (node_json == NULL) {
        spin_log(LOG_ERR, "Unable to parse node data\n");
        goto end;
    }

    newnode = node_create(0);

    // id
    id_json = cJSON_GetObjectItemCaseSensitive(node_json, "id");
    if (!cJSON_IsNumber(id_json)) {
        goto end;
    }
    newnode->id = id_json->valueint;

    // mac
    mac_json = cJSON_GetObjectItemCaseSensitive(node_json, "mac");
    if (!cJSON_IsString(mac_json)) {
        goto end;
    }
    node_set_mac(newnode, mac_json->valuestring);

    // name
    name_json = cJSON_GetObjectItemCaseSensitive(node_json, "name");
    if (!cJSON_IsString(name_json)) {
        goto end;
    }
    node_set_name(newnode, name_json->valuestring);

    // ips
    ips_json = cJSON_GetObjectItemCaseSensitive(node_json, "ips");
    if (!cJSON_IsArray(ips_json)) {
        goto end;
    }
    cJSON_ArrayForEach(ip_json, ips_json) {
        ip_t ipval;

        if (!cJSON_IsString(ip_json) || !spin_pton(&ipval, ip_json->valuestring)) {
            goto end;
        }
        node_add_ip(newnode, &ipval);
    }


    // domains
    domains_json = cJSON_GetObjectItemCaseSensitive(node_json, "domains");
    if (!cJSON_IsArray(domains_json)) {
        goto end;
    }
    cJSON_ArrayForEach(domain_json, domains_json) {
        if (!cJSON_IsString(ip_json)) {
            goto end;
        }
        node_add_domain(newnode, domain_json->valuestring);
    }

end:
    cJSON_Delete(node_json);
    return (node_t *) newnode;
}

#else /* CJS */

#include "jsmn.h"
#define NTOKENS 200

// Do longest keywords here first, to prevent initial substring matching
enum nkw {
    NKW_IS_EXCEPTED,
    NKW_IS_BLOCKED,
    NKW_LASTSEEN,
    NKW_DOMAINS,
    NKW_NAME,
    NKW_MAC,
    NKW_IPS,
    NKW_ID,
    N_NKW
};

char *nodekeyw[] = {
[NKW_IS_EXCEPTED] = "is_excepted",
[NKW_IS_BLOCKED] = "is_blocked",
[NKW_LASTSEEN] = "lastseen",
[NKW_DOMAINS] = "domains",
[NKW_NAME] = "name",
[NKW_MAC] = "mac",
[NKW_IPS] = "ips",
[NKW_ID] = "id",
};

static enum nkw
find_nkw(char *begin, int len) {
    int i;
    const char * kw;
    int klen;

    for (i=0; i<N_NKW; i++) {
        kw = nodekeyw[i];
        klen = strlen(kw);
        if (strncmp(kw, begin, klen) == 0) {
            return i;
        }
    }
    return i;
}

static char *
find_str(char *s, char *e) {

    *e = 0; /* Should be safe */
    return s;
}

static int
find_num(char *s, char *e) {

    return atoi(find_str(s,e));
}

#define CHECK_TYPE(n, t) if (tokens[n].type != t) { spin_log(LOG_ERR, "Token %d bad type\n", n); return 0; }
#define CHECK_TRUTH(tr) if (!(tr)) { spin_log(LOG_ERR, "Unknown error in parse\n"); return 0; }

static node_t*
decode_node_info(char *data, int datalen) {
    jsmn_parser p;
    jsmntok_t tokens[NTOKENS];
    int result;
    int i, aix;
    int nexttok;
    node_t *newnode;
    char *mac, *name;
    int old_id;

    jsmn_init(&p);
    result = jsmn_parse(&p, data, datalen, tokens, NTOKENS);
    if (result < 0) {
        spin_log(LOG_ERR, "Unable to parse node data\n");
        return 0;
    }
    CHECK_TYPE(0, JSMN_OBJECT);

    newnode = node_create(0);
    nexttok = 1;
    for (i=0; i<tokens[0].size; i++) {
        int key, val;
        enum nkw kw;

        key = nexttok;
        CHECK_TYPE(key, JSMN_STRING);
        kw = find_nkw(data+tokens[key].start, tokens[key].end - tokens[key].start);
        CHECK_TRUTH(kw!=N_NKW);
        nexttok++;
        val = nexttok;
        switch(kw) {
        case NKW_ID:
            // Setup mapping between old and new numbers
            CHECK_TYPE(val, JSMN_PRIMITIVE);
            old_id = find_num(data+tokens[val].start, data+tokens[val].end);
            newnode->id = old_id;
            break;
        case N_NKW:
            // Cannot happen, but compiler wants it
        case NKW_IS_EXCEPTED:
        case NKW_IS_BLOCKED:
        case NKW_LASTSEEN:
            // These will be set again by other software
            break;
        case NKW_MAC:
            // Store mac Address
            CHECK_TYPE(val, JSMN_STRING);
            mac = find_str(data+tokens[val].start, data+tokens[val].end);
            node_set_mac(newnode, mac);
            break;
        case NKW_NAME:
            // Store name
            CHECK_TYPE(val, JSMN_STRING);
            name = find_str(data+tokens[val].start, data+tokens[val].end);
            node_set_name(newnode, name);
            break;
        case NKW_IPS:
            // Store IP addresses
            CHECK_TYPE(val, JSMN_ARRAY);
            for (aix=1; aix <= tokens[val].size; aix++) {
                char *ipaddr;
                ip_t ipval;
                int retval;

                CHECK_TYPE(val+aix, JSMN_STRING);
                ipaddr = find_str(data+tokens[val+aix].start, data+tokens[val+aix].end);
                retval = spin_pton(&ipval, ipaddr);
                CHECK_TRUTH(retval);
                node_add_ip(newnode, &ipval);
            }
            nexttok = val+aix-1;
            break;
        case NKW_DOMAINS:
            // Store IP addresses
            CHECK_TYPE(val, JSMN_ARRAY);
            for (aix=1; aix <= tokens[val].size; aix++) {
                char *domainname;

                CHECK_TYPE(val+aix, JSMN_STRING);
                domainname = find_str(data+tokens[val+aix].start, data+tokens[val+aix].end);
                node_add_domain(newnode, domainname);
            }
            nexttok = val+aix-1;
            break;
        }
        nexttok++;
    }
    return newnode;
}

#endif /* CJS */

#define NODE_FILENAME_DIR "/etc/spin/nodestore"
#define NODEPAIRFILE "/etc/spin/nodepair.list"

static char node_filename[100];
static char *
node_filename_int(int nodenum) {

    sprintf(node_filename, "%s/%d", NODE_FILENAME_DIR, nodenum);
    return node_filename;
}

static char *
node_filename_str(char *nodenum) {

    sprintf(node_filename, "%s/%s", NODE_FILENAME_DIR, nodenum);
    return node_filename;
}

static void
store_node_info(int nodenum, buffer_t *node_json) {
    FILE *nodefile;

    mkdir(NODE_FILENAME_DIR, 0777);
    nodefile = fopen(node_filename_int(nodenum), "w");
    fprintf(nodefile, "%s\n", buffer_str(node_json));
    fclose(nodefile);
}

/*
 * End of node info store code
 */

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

static void
send_command_node_info(int nodenum, buffer_t *node_json) {
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    char mosqchan[100];

    create_mqtt_command(response_json, "nodeInfo", NULL, buffer_str(node_json));
    if (buffer_finish(response_json)) {
        // Subdivide channel
        sprintf(mosqchan, "SPIN/traffic/node/%d", nodenum);
        pubsub_publish(mosqchan,
                buffer_size(response_json), buffer_str(response_json), 1);
    } else {
        spin_log(LOG_WARNING, "Error converting nodeInfo to JSON; partial packet: %s\n", buffer_str(response_json));
    }
    buffer_destroy(response_json);
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

static void
node_is_updated(node_t *node) {
    buffer_t* node_json = buffer_create(JSONBUFSIZ);
    unsigned int p_size;

    p_size = node2json(node, node_json);
    if (p_size > 0) {
        buffer_finish(node_json);
        // work
        send_command_node_info(node->id, node_json);
        if (node->persistent) /* Persistent? */ {
            // Store modified node in file
            store_node_info(node->id, node_json);

            // Update IP addresses in c2b
            update_node_ips(node->id, node->ips);
        }
    } else {
        spin_log(LOG_DEBUG, "[XX] node2json failed(size 0)\n");
    }
    buffer_destroy(node_json);
}

static void
publish_nodes() {

    // Currently called just before traffic messages
    // Maybe also with timer?
    //
    // If so, how often?

    node_callback_new(node_cache, node_is_updated);
}

/*
 * RPC implementing code
 *
 * This should probably be moved to separate file
 *
 */
tree_t *nodepair_tree;
tree_t *nodemap_tree;

static void
handle_node_info(char *buf, int size) {
    node_t *newnode;
    int oldid, newid;
    int wasnew;

    newnode = decode_node_info(buf, size);

    if (newnode) {
        oldid = newnode->id;
        newnode->id = 0;
        wasnew = node_cache_add_node(node_cache, newnode);
        // What if it was merged??
        assert(wasnew);
        newid = newnode->id;
        tree_add(nodemap_tree,
            sizeof(oldid), (void *) &oldid,
            sizeof(newid), (void *) &newid,
            1);
    }
}

#define NODE_READ_SIZE (JSONBUFSIZ+1000)

static void
retrieve_node_info() {
    DIR *nodedir;
    struct dirent *fentry;
    char data[NODE_READ_SIZE];
    int fildes;
    int numbytes;
    char *filename;

    nodedir = opendir(NODE_FILENAME_DIR);
    if (nodedir == NULL) {
        spin_log(LOG_ERR, "Could not open directory %s\n", NODE_FILENAME_DIR);
        return;
    }
    while ((fentry = readdir(nodedir))!=NULL) {
        if (fentry->d_name[0] == '.') {
            continue;
        }
        filename = node_filename_str(fentry->d_name);
        fildes = open(filename, 0);
        if (fildes >= 0) {
            // do something with file
            // read it, decode it, remove it
            numbytes = read(fildes, data, NODE_READ_SIZE);
            // If it totally fills buffer it is too large
            if (numbytes < NODE_READ_SIZE) {
                // zero terminate, just in case
                data[numbytes] = 0;
                handle_node_info(data, numbytes);
            }
            close(fildes);
        }
        unlink(filename);
    }
    closedir(nodedir);
}

static int map_node(int nodenum) {
    tree_entry_t *leaf;
    int newnodenum;

    leaf = tree_find(nodemap_tree, sizeof(nodenum), (void *) &nodenum);
    if (leaf == NULL) {
        spin_log(LOG_ERR, "COuld not find mapping for node %d\n", nodenum);
        return 0;
    }
    newnodenum = *((int *) leaf->data);
    return newnodenum;
}

int read_nodepair_tree(const char *filename) {
    int count = 0;
    char line[100];
    char* rline;
    int id[2];
    int node1, node2;
    int spinrpc_blockflow(int node1, int node2, int block);

    FILE* in = fopen(filename, "r");
    if (in == NULL) {
        return -1;
    }
    unlink(filename); // to prevent overwriting TODO
    while ((rline = fgets(line, sizeof(line), in)) != NULL) {
        if (sscanf(rline, "%d %d", &id[0], &id[1]) == 2) {
            node1 = map_node(id[0]);
            node2 = map_node(id[1]);
            spinrpc_blockflow(node1, node2, 1);
            // Do mapping and actual blocking
            // tree_add(nodepair_tree, sizeof(id), (void *) id, 0, NULL, 1);
            count++;
        }
    }
    fclose(in);
    return count;
}

static void
init_spinrpc() {

    nodepair_tree = tree_create(cmp_2ints);

    // Make mapping tree while doing init
    nodemap_tree = tree_create(cmp_ints);
    retrieve_node_info();

    read_nodepair_tree(NODEPAIRFILE);

    tree_destroy(nodemap_tree);
}

static int
change_node_persistency(int nodenum, int val) {
    node_t *node;
    time_t now;

    node = node_cache_find_by_id(node_cache, nodenum);
    if (node != NULL) {
        if (node->persistent == 0) {
            // Is becoming persistent
            now = time(NULL);
            node_set_modified(node, now);
            c2b_node_persistent_start(nodenum);
        }
        node->persistent += val;
        if (node->persistent == 0) {
            // Stops being persistent
            now = time(NULL);
            node_set_modified(node, now);
            c2b_node_persistent_end(nodenum);
        }
        return 0;
    }
    return 1;
}

static void
store_nodepairs() {

    store_nodepair_tree(nodepair_tree, NODEPAIRFILE);
}

static int
spinrpc_blockflow_start(int node1, int node2) {
    int result;
    int node_ar[2];
    tree_entry_t *leaf;

    node_ar[0] = node1;
    node_ar[1] = node2;
    leaf = tree_find(nodepair_tree, sizeof(node_ar), (void *) node_ar);
    if (leaf != NULL) {
        // This flow was already blocked
        return 0;
    }

    result = 0;
    result += change_node_persistency(node1, 1);
    result += change_node_persistency(node2, 1);
    if (result) {
        return result;
    }

    // Copy flag is 1, key must be copied
    tree_add(nodepair_tree, sizeof(node_ar), (void *) node_ar, 0, NULL, 1);
    // new pair to block
    store_nodepairs();
    c2b_blockflow_start(node1, node2);
    return 0;
}

static int
spinrpc_blockflow_stop(int node1, int node2) {
    int result;
    int node_ar[2];
    tree_entry_t *leaf;

    node_ar[0] = node1;
    node_ar[1] = node2;
    leaf = tree_find(nodepair_tree, sizeof(node_ar), (void *) node_ar);
    if (leaf == NULL) {
        // This flow was not blocked
        return 1;
    }
    // Remove from tree
    tree_remove_entry(nodepair_tree, leaf);
    store_nodepairs();
    c2b_blockflow_end(node1, node2);

    result = 0;
    result += change_node_persistency(node1, -1);
    result += change_node_persistency(node2, -1);
    return result;
}

int
spinrpc_blockflow(int node1, int node2, int block) {
    int result;
    int sn1, sn2;
    STAT_COUNTER(ctr, rpc-blockflow, STAT_TOTAL);

    if (node1 == node2) {
        return 1;
    }
    if (node1 < node2) {
        sn1 = node1; sn2 = node2;
    } else {
        sn1 = node2; sn2 = node1;
    }
    if (block) {
        result = spinrpc_blockflow_start(sn1, sn2);
    } else {
        result = spinrpc_blockflow_stop(sn1, sn2);
    }
    STAT_VALUE(ctr, 1);
    return result;
}

static void
nodepairtree2json(tree_t* tree, buffer_t* result) {
    tree_entry_t* cur;
    char *prefix;
    int *nodenump;

    cur = tree_first(tree);

    if (cur == NULL) {
        // empty tree
        buffer_write(result, " [ ] ");
        return;
    }

    // Prefix is [ at first, after that ,
    prefix = " [ ";

    while (cur != NULL) {
        nodenump = (int *) cur->key;
        buffer_write(result, prefix);
        buffer_write(result, "{ \"node1\": %d, \"node2\": %d}", nodenump[0], nodenump[1]);
        prefix = " , ";
        cur = tree_next(cur);
    }
    buffer_write(result, " ] ");
}

char *
spinrpc_get_blockflow() {
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    char *retval;

    buffer_write(response_json, "{\"result\": ");
    nodepairtree2json(nodepair_tree, response_json);
    buffer_write(response_json, "}");
    if (buffer_finish(response_json)) {
        retval = strdup(buffer_str(response_json));
    } else {
        retval = "{ -1 }";
    }
    return strdup(retval);
}

/*
 * End of RPC code
 */

void send_command_blocked(pkt_info_t* pkt_info) {
    buffer_t* response_json = buffer_create(JSONBUFSIZ);
    buffer_t* pkt_json = buffer_create(JSONBUFSIZ);
    unsigned int p_size;

    // Publish recently changed nodes
    publish_nodes();

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

    // Publish recently changed nodes
    publish_nodes();

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
    STAT_COUNTER(ctr1, send-flow, STAT_TOTAL);
    STAT_COUNTER(ctr2, create-traffic, STAT_TOTAL);

    if (flow_list_should_send(flow_list, now)) {
        STAT_VALUE(ctr1, 1);
        if (!flow_list_empty(flow_list)) {
            buffer_t* json_buf = buffer_create(JSONBUFSIZ);

            buffer_allow_resize(json_buf);

            // Publish recently changed nodes
            publish_nodes();

            // create json, send it
            STAT_VALUE(ctr2, 1);
            create_traffic_command(node_cache, flow_list, json_buf, now);
            if (buffer_finish(json_buf)) {
                core2pubsub_publish(json_buf);
                // mosq_result = mosquitto_publish(mosq, NULL, MQTT_CHANNEL_TRAFFIC, buffer_size(json_buf), buffer_str(json_buf), 0, false);
            }
            buffer_destroy(json_buf);
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

/*
 * The three lists of IP addresses are now kept in memory in spind, with
 * a copy written to file.
 * BLOCK, IGNORE, ALLOW
 */
// TODO: move this out to the library, this feels like pretty commonly shared code
struct list_info {
    tree_t *        li_tree;                 // Tree of IP addresses
    char *          li_filename;             // Name of shadow file
    int             li_modified;     // File should be written
} ipl_list_ar[N_IPLIST] = {
    { 0, "/etc/spin/block.list",   0 },
    { 0, "/etc/spin/ignore.list",  0 },
    { 0, "/etc/spin/allow.list",   0 },
};

#define ipl_block ipl_list_ar[IPLIST_BLOCK]
#define ipl_ignore ipl_list_ar[IPLIST_IGNORE]
#define ipl_allow ipl_list_ar[IPLIST_ALLOW]

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

void init_ipl(struct list_info *lip) {
    int cnt;

    lip->li_tree = tree_create(cmp_ips);
    cnt = read_ip_tree(lip->li_tree, lip->li_filename);
    spin_log(LOG_DEBUG, "File %s, read %d entries\n", lip->li_filename, cnt);
}

void init_all_ipl() {
    int i;
    struct list_info *lip;

    for (i=0; i<N_IPLIST; i++) {
        lip = &ipl_list_ar[i];
        init_ipl(lip);
    }

    // Sync trees to files every 2.5 seconds for now
    mainloop_register("IP list sync", wf_ipl, (void *) 0, 0, 2500);
}

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

    if (tree == NULL) {
        return;
    }
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

int ip_in_li(ip_t* ip, struct list_info* lip) {

    return tree_find(lip->li_tree, sizeof(ip_t), ip) != NULL;
}

int ip_in_ignore_list(ip_t* ip) {

    return ip_in_li(ip, &ipl_list_ar[IPLIST_IGNORE]);
}

// hmz, we probably want to refactor ip_t/the tree list
// into something that requires less data copying
int addr_in_ignore_list(int family, uint8_t* addr) {
    ip_t ip;

    ip.family = family;
    memcpy(ip.addr, addr, 16);
    return ip_in_ignore_list(&ip);
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

void ubus_main();

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

    SPIN_STAT_START();

    init_cache();

    init_core2conntrack(node_cache, local_mode);
    init_core2nflog_dns(node_cache, dns_cache);

    init_core2block();

    init_all_ipl();

    init_spinrpc();

    init_mosquitto(mosq_host, mosq_port);
    signal(SIGINT, int_handler);

    push_all_ipl();

    omitnode = spinconfig_pubsub_omitnode();

    ubus_main();

    mainloop_run();

    cleanup_cache();
    cleanup_core2block();

    finish_mosquitto();

    SPIN_STAT_FINISH();

    return 0;
}
