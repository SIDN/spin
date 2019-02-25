#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "util.h"
#include "handle_command.h"
#include "spin_log.h"
#include "mainloop.h"
#include "nfqroutines.h"

#include "spin_list.h"
#include "spind.h"
#include "spinconfig.h"

#define MAXSTR 1024

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
 */
static int ignore_system_errors;

static void
iptab_system(char *s) {
    int result;

    if (dolog) {
        fprintf(logfile, "%s\n", s);
    }
    result = system(s);
    assert (ignore_system_errors || result == 0);
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

static void
iptab_add_jump(char *table, int option, char *cond, char *dest) {
    char str[MAXSTR];
    static char *iaj_option[3] = { "-A", "-I", "-D" };

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

static void
clean_old_tables(int queue_dns) {
    char nfq_queue_str[MAXSTR];

    iptab_do_table(SpinCheck, IDT_FLUSH);
    iptab_do_table(SpinLog, IDT_FLUSH);
    iptab_do_table(SpinBlock, IDT_FLUSH);

    iptab_add_jump(table_input, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_output, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_forward, IAJ_DEL, 0, SpinCheck);

    sprintf(nfq_queue_str, "NFQUEUE --queue-bypass --queue-num %d", queue_dns);
    iptab_add_jump(table_output, IAJ_DEL, "-p udp --sport 53", nfq_queue_str);
    iptab_add_jump(table_input, IAJ_DEL, "-p udp --dport 53", nfq_queue_str);
    iptab_add_jump(table_forward, IAJ_DEL, "-p udp --dport 53", nfq_queue_str);

    iptab_do_table(SpinCheck, IDT_DEL);
    iptab_do_table(SpinLog, IDT_DEL);
    iptab_do_table(SpinBlock, IDT_DEL);
}

static void
setup_tables(int queue_dns, int queue_block, int place) {
    char str[MAXSTR];
    char nfq_queue_str[MAXSTR];
    int block_iaj;

    // Currently block and dns both in one list
    block_iaj = place ? IAJ_ADD : IAJ_INS;

    ignore_system_errors = 1;
    clean_old_tables(queue_dns);
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
    sprintf(nfq_queue_str, "NFQUEUE --queue-bypass --queue-num %d", queue_dns);
    iptab_add_jump(table_output, IAJ_INS, "-p udp --sport 53", nfq_queue_str);
    iptab_add_jump(table_input, IAJ_INS, "-p udp --dport 53", nfq_queue_str);
    iptab_add_jump(table_forward, IAJ_INS, "-p udp --dport 53", nfq_queue_str);

    iptab_do_table(SpinBlock, IDT_MAKE);
    iptab_add_jump(SpinBlock, IAJ_ADD, 0, SpinLog);
    sprintf(str, "NFQUEUE --queue-num %d", queue_block);
    iptab_add_jump(SpinBlock, IAJ_ADD, 0, str);
}

static char *iptables_command[2] = { "iptables", "ip6tables" };
static char *srcdst[2] = { "-s", "-d" };

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


    // IP v4 or 6, decode address
    if (ip_addr->family != AF_INET) {
        ipv6 = 1;
    }
    spin_ntop(ip_str, ip_addr, INET6_ADDRSTRLEN);
    spin_log(LOG_DEBUG, "Change list %d %d %d %s\n", iplist, addrem, ipv6, ip_str);

    c2b_do_rule(tables[iplist], ipv6, addrem, ip_str, targets[iplist]);
}

static int
c2b_catch(void *arg, int af, int proto, uint8_t* data, int size, uint8_t *src_addr, uint8_t *dest_addr, unsigned src_port, unsigned dest_port) {

    spin_log(LOG_DEBUG, "c2b_catch %d %d %d %d (%x, %x, %x) %d\n", af, proto, src_port, dest_port, data[0], data[1], data[2], size);
    report_block(af, proto, src_addr, dest_addr, src_port, dest_port, size);
    return 0;           // DROP
}

static void
setup_catch(int queue) {

    // Here we set up the catching of kernel messages for LOGed packets
    nfqroutine_register("core2block", c2b_catch, (void *) 0, queue);
}

static void
wf_core2block(void *arg, int data, int timeout) {

    if (timeout) {
        spin_log(LOG_DEBUG, "wf_core2block called\n");
        // TODO Do something with kernel messages
    }
}

void init_core2block() {
    static int all_lists[N_IPLIST] = { 1, 1, 1 };
    int queue_dns, queue_block;
    int place_dns, place_block;

    queue_dns = spinconfig_iptable_queue_dns();
    queue_block = spinconfig_iptable_queue_block();
    place_dns = spinconfig_iptable_place_dns();
    place_block = spinconfig_iptable_place_block();

    // TODO: is place_dns necessary? unused at this moment
    // silence unused warning:
    (void)place_dns;

    spin_log(LOG_DEBUG, "NFQ's %d and %d\n", queue_dns, queue_block);

    setup_catch(queue_block);
    setup_debug();
    setup_tables(queue_dns, queue_block, place_block);

    mainloop_register("core2block", wf_core2block, (void *) 0, 0, 10000);
    spin_register("core2block", c2b_changelist, (void *) 0, all_lists);
}

void cleanup_core2block() {
}
