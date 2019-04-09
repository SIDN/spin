#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "util.h"
#include "handle_command.h"
#include "spin_log.h"
#ifdef notdef
#include "mainloop.h"
#endif
#include "nfqroutines.h"

#include "spin_list.h"
#include "spind.h"
#include "spinconfig.h"

#include "statistics.h"
// needed for the CORE2NFLOG_DNS_GROUP_NUMBER value
// (we will probably make this configurable)
#include "core2nflog_dns.h"

#define MAXSTR 1024

STAT_MODULE(core2block)
// static const char stat_modname[]="core2block";

static int dolog;
static FILE *logfile;

static void
setup_debug() {
    char *fname;

    fname = spinconfig_iptable_debug();
    if (fname == 0 || *fname == 0)  {
        return;
    }

    logfile = fopen(fname, "w");
    setbuf(logfile, NULL);
    dolog = 1;
}

/*
 * At startup cleaning up could cause errors
 *
 * Also sometimes more addresses are deleted, TODO
 */
static int ignore_system_errors;

static void
iptab_system(char *s) {
    int result;
    STAT_COUNTER(ctr, system, STAT_TOTAL);
    // static stat_t ctr = { stat_modname, "system", STAT_TOTAL };

    STAT_VALUE(ctr, 1);
    // stat_val(&ctr, 1);
    result = system(s);
    if (dolog) {
        char *resstr = result ? " -> ERROR" : " -> OK";
        fprintf(logfile, "%s%s\n", s, ignore_system_errors ? "" : resstr);
    }
}

#define IDT_MAKE        0
#define IDT_DEL         1
#define IDT_FLUSH       2

static void
iptab_do_table(char *name, int delete) {
    char str[MAXSTR];
    static char *imt_option[3] = { "-N", "-X", "-F" };

    sprintf(str, "iptables %s %s", imt_option[delete], name);
    iptab_system(str);
    sprintf(str, "ip6tables %s %s", imt_option[delete], name);
    iptab_system(str);
}

#define IAJ_ADD 0
#define IAJ_INS 1
#define IAJ_DEL 2
static char *iaj_option[3] = { "-A", "-I", "-D" };

static void
iptab_add_jump(char *table, int option, char *cond, char *dest) {
    char str[MAXSTR];

    sprintf(str, "iptables %s %s%s%s -j %s", iaj_option[option], table,
                        cond ? " " : "", cond ? cond : "", dest);
    iptab_system(str);
    sprintf(str, "ip6tables %s %s%s%s -j %s", iaj_option[option], table,
                        cond ? " " : "", cond ? cond : "", dest);
    iptab_system(str);
}

static char *table_input = "INPUT";
static char *table_output = "OUTPUT";
static char *table_forward = "FORWARD";

static char SpinCheck[] = "SpinCheck";
static char SpinBlock[] = "SpinBlock";
static char SpinLog[] = "SpinLog";
static char Return[] = "RETURN";

static char *iptables_command[2] = { "iptables", "ip6tables" };
static char *srcdst[2] = { "-s", "-d" };

// TODO: rename nflog_dns_group to nflog_dns_group
static void
clean_old_tables(int nflog_dns_group) {
    char nfq_queue_str[MAXSTR];

    iptab_do_table(SpinCheck, IDT_FLUSH);
    iptab_do_table(SpinLog, IDT_FLUSH);
    iptab_do_table(SpinBlock, IDT_FLUSH);

    iptab_add_jump(table_input, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_output, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_forward, IAJ_DEL, 0, SpinCheck);

    sprintf(nfq_queue_str, "NFLOG --nflog-group %d", nflog_dns_group);
    iptab_add_jump(table_output, IAJ_DEL, "-p udp --sport 53", nfq_queue_str);
    iptab_add_jump(table_input, IAJ_DEL, "-p udp --dport 53", nfq_queue_str);
    iptab_add_jump(table_forward, IAJ_DEL, "-p udp --dport 53", nfq_queue_str);

    iptab_do_table(SpinCheck, IDT_DEL);
    iptab_do_table(SpinLog, IDT_DEL);
    iptab_do_table(SpinBlock, IDT_DEL);
}

static char *ipset_name(int nodenum, int v6) {
    static char namebuf[2][100];
    static int which;

    // Be prepared for two "simultaneous" calls
    // Hack...
    which = (which+1)%2;
    sprintf(namebuf[which], "N%dV%d", nodenum, v6 ? 6 : 4);
    return namebuf[which];
}

static void
ipset_create(int nodenum, int v6) {
    char str[MAXSTR];
    STAT_COUNTER(ctr, set-create, STAT_TOTAL);

    sprintf(str, "ipset create %s hash:ip family %s", ipset_name(nodenum, v6), v6? "inet6" : "inet");
    iptab_system(str);
    STAT_VALUE(ctr, 1);
}

static void
ipset_destroy(int nodenum, int v6) {
    char str[MAXSTR];
    STAT_COUNTER(ctr, set-destroy, STAT_TOTAL);

    sprintf(str, "ipset destroy %s", ipset_name(nodenum, v6));
    iptab_system(str);
    STAT_VALUE(ctr, 1);
}

static void
ipset_add_addr(int nodenum, int v6, char *addr) {
    char str[MAXSTR];
    STAT_COUNTER(ctr, set-add-addr, STAT_TOTAL);

    sprintf(str, "ipset add -exist %s %s", ipset_name(nodenum, v6), addr);
    iptab_system(str);
    STAT_VALUE(ctr, 1);
}

static void
ipset_blockflow(int v6, int option, int nodenum1, int nodenum2) {
    char str[MAXSTR];
    int i;
    static char *sd[] = { "src", "dst", "src" };
    static char matchset[] = "-m set --match-set";
    STAT_COUNTER(ctr, set-block-flow, STAT_TOTAL);

    for (i=0; i<2; i++) {
        // Both directions
        sprintf(str, "%s %s %s %s %s %s %s %s %s -j %s",
            iptables_command[v6],
            iaj_option[option], SpinCheck, 
            matchset, ipset_name(nodenum1, v6), sd[i],
            matchset, ipset_name(nodenum2, v6), sd[i+1],
            SpinBlock);
        iptab_system(str);
    }
    STAT_VALUE(ctr, 1);
}

static void
setup_tables(int nflog_dns_group, int queue_block, int place) {
    char str[MAXSTR];
    char nfq_queue_str[MAXSTR];
    int block_iaj;

    // Currently block and dns both in one list
    block_iaj = place ? IAJ_ADD : IAJ_INS;

    ignore_system_errors = 1;
    clean_old_tables(nflog_dns_group);
    ignore_system_errors = 0;

    iptab_do_table(SpinCheck, IDT_MAKE);
    iptab_add_jump(table_input, block_iaj, 0, SpinCheck);
    iptab_add_jump(table_output, block_iaj, 0, SpinCheck);
    iptab_add_jump(table_forward, block_iaj, 0, SpinCheck);

    iptab_do_table(SpinLog, IDT_MAKE);
    iptab_add_jump(SpinLog, IAJ_ADD, 0, "LOG --log-prefix \"Spin blocked: \"");
    // Forward all (udp) DNS queries to nfqueue (for core2nfq_dns)
    // Note: only UDP for now, we'll need to reconstruct TCP packets
    // to support that
    sprintf(nfq_queue_str, "NFLOG --nflog-group %d", nflog_dns_group);
    iptab_add_jump(table_output, IAJ_INS, "-p udp --sport 53", nfq_queue_str);
    iptab_add_jump(table_input, IAJ_INS, "-p udp --dport 53", nfq_queue_str);
    iptab_add_jump(table_forward, IAJ_INS, "-p udp --dport 53", nfq_queue_str);

    iptab_do_table(SpinBlock, IDT_MAKE);
    iptab_add_jump(SpinBlock, IAJ_ADD, 0, SpinLog);
    sprintf(str, "NFQUEUE --queue-num %d", queue_block);
    iptab_add_jump(SpinBlock, IAJ_ADD, 0, str);
}

static void
c2b_do_rule(char *table, int ipv6, int addrem, char *ip_str, char *target) {
    char *cmd, *flag;
    char str[MAXSTR];
    int i;

    cmd = iptables_command[ipv6];
    flag = addrem == SF_ADD ? "-I": "-D";
    for (i=0; i<2; i++) {
        sprintf(str, "%s %s %s %s %s -j %s", cmd, flag, table, srcdst[i], ip_str, target);
        iptab_system(str);
    }
}

static char *targets[] = { SpinBlock, Return, Return };
static char *tables[] = { SpinCheck, SpinLog, SpinBlock };

// Entry point
void c2b_changelist(void* arg, int iplist, int addrem, ip_t *ip_addr) {
    int ipv6 = 0;
    char ip_str[INET6_ADDRSTRLEN];
    STAT_COUNTER(ctr, changelist, STAT_TOTAL);

    // IP v4 or 6, decode address
    if (ip_addr->family != AF_INET) {
        ipv6 = 1;
    }
    spin_ntop(ip_str, ip_addr, INET6_ADDRSTRLEN);
    spin_log(LOG_DEBUG, "Change list %d %d %d %s\n", iplist, addrem, ipv6, ip_str);

    STAT_VALUE(ctr, 1);
    c2b_do_rule(tables[iplist], ipv6, addrem, ip_str, targets[iplist]);
}

void c2b_node_persistent_start(int nodenum) {

    // Make the Ipv4 and Ipv6 ipsets for this node
    ipset_create(nodenum, 0);
    ipset_create(nodenum, 1);
}

void c2b_node_persistent_end(int nodenum) {

    // Remove the ipsets
    ipset_destroy(nodenum, 0);
    ipset_destroy(nodenum, 1);
}

void c2b_node_ipaddress(int nodenum, ip_t *ip_addr) {
    int ipv6 = 0;
    char ip_str[INET6_ADDRSTRLEN];

    // Add or re-add ip-addr to nodenum's set

    // IP v4 or 6, decode address
    if (ip_addr->family != AF_INET) {
        ipv6 = 1;
    }
    spin_ntop(ip_str, ip_addr, INET6_ADDRSTRLEN);
    spin_log(LOG_DEBUG, "c2b_node_ipaddress %d %d %s\n", nodenum, ipv6, ip_str);

    ipset_add_addr(nodenum, ipv6, ip_str);
}

void c2b_flowblock_start(int nodenum1, int nodenum2) {

    // Block this flow
    ipset_blockflow(0, IAJ_INS, nodenum1, nodenum2);
    ipset_blockflow(1, IAJ_INS, nodenum1, nodenum2);
}

void c2b_flowblock_end(int nodenum1, int nodenum2) {

    // Unblock this flow
    ipset_blockflow(0, IAJ_DEL, nodenum1, nodenum2);
    ipset_blockflow(1, IAJ_DEL, nodenum1, nodenum2);
}

static int
c2b_catch(void *arg, int af, int proto, uint8_t* data, int size, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port) {
    STAT_COUNTER(ctr, catch-block, STAT_TOTAL);

    spin_log(LOG_DEBUG, "c2b_catch %d %d %d %d (%x, %x, %x) %d\n", af, proto, src_port, dest_port, data[0], data[1], data[2], size);
    STAT_VALUE(ctr, 1);
    report_block(af, proto, src_addr, dest_addr, src_port, dest_port, size);
    return 0;           // DROP
}

static void
setup_catch(int queue) {

    // Here we set up the catching of kernel messages for LOGed packets
    nfqroutine_register("core2block", c2b_catch, (void *) 0, queue);
}

#ifdef notdef
static void
wf_core2block(void *arg, int data, int timeout) {

    if (timeout) {
        spin_log(LOG_DEBUG, "wf_core2block called\n");
        // TODO Do something with kernel messages
    }
}
#endif

void init_core2block() {
    static int all_lists[N_IPLIST] = { 1, 1, 1 };
    int nflog_dns_group, queue_block;
    int place_dns, place_block;

    nflog_dns_group = spinconfig_iptable_nflog_dns_group();
    queue_block = spinconfig_iptable_queue_block();
    place_dns = spinconfig_iptable_place_dns();
    place_block = spinconfig_iptable_place_block();

    // TODO: is place_dns necessary? unused at this moment
    // silence unused warning:
    (void)place_dns;

    spin_log(LOG_DEBUG, "NFQ's %d and %d\n", nflog_dns_group, queue_block);

    setup_catch(queue_block);
    setup_debug();
    setup_tables(nflog_dns_group, queue_block, place_block);

#ifdef notdef
    mainloop_register("core2block", wf_core2block, (void *) 0, 0, 10000);
#endif
    spin_register("core2block", c2b_changelist, (void *) 0, all_lists);
}

void cleanup_core2block() {

    // Add freeing of malloced memory for memory-leak detection
}
