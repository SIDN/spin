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

#define MAXSTR 1024

// #define SF_ADD			0
// #define SF_REM			1

#define QUEUE_BLOCK	2

FILE *logfile;

static void
setup_debug() {

    logfile = fopen("/tmp/block_commands", "w");
    setbuf(logfile, NULL);
}

static int ignore_system_errors;

static void
iptab_system(char *s) {
    int result;

    fprintf(logfile, "%s\n", s);
    result = system(s);
    assert (ignore_system_errors || result == 0);
}

#define IDT_MAKE	0
#define IDT_DEL		1
#define IDT_FLUSH	2

static void
iptab_do_table(char *name, int delete) {
    char str[MAXSTR];
    static char *imt_option[3] = { "-N", "-X", "-F" };

    sprintf(str, "iptables %s %s", imt_option[delete], name);
    iptab_system(str);
    sprintf(str, "ip6tables %s %s", imt_option[delete], name);
    iptab_system(str);
}

#define IAJ_ADD	0
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
clean_old_tables() {

    iptab_do_table(SpinCheck, IDT_FLUSH);
    iptab_do_table(SpinLog, IDT_FLUSH);
    iptab_do_table(SpinBlock, IDT_FLUSH);

    iptab_add_jump(table_input, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_output, IAJ_DEL, 0, SpinCheck);
    iptab_add_jump(table_forward, IAJ_DEL, 0, SpinCheck);

    iptab_do_table(SpinCheck, IDT_DEL);
    iptab_do_table(SpinLog, IDT_DEL);
    iptab_do_table(SpinBlock, IDT_DEL);
}

static void
setup_tables() {
    char str[MAXSTR];

    ignore_system_errors = 1;
    clean_old_tables();
    ignore_system_errors = 0;

    iptab_do_table(SpinCheck, IDT_MAKE);
    iptab_add_jump(table_input, IAJ_INS, 0, SpinCheck);
    iptab_add_jump(table_output, IAJ_INS, 0, SpinCheck);
    iptab_add_jump(table_forward, IAJ_INS, 0, SpinCheck);

    iptab_do_table(SpinLog, IDT_MAKE);
    iptab_add_jump(SpinLog, IAJ_ADD, 0, "LOG --log-prefix \"Spin blocked: \"");

    iptab_do_table(SpinBlock, IDT_MAKE);
    iptab_add_jump(SpinBlock, IAJ_ADD, 0, SpinLog);
    sprintf(str, "NFQUEUE --queue-num %d", QUEUE_BLOCK);
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
    if (ip_addr->family != AF_INET)
	ipv6 = 1;
    spin_ntop(ip_str, ip_addr, INET6_ADDRSTRLEN);
    spin_log(LOG_DEBUG, "Change list %d %d %d %s\n", iplist, addrem, ipv6, ip_str);

    c2b_do_rule(tables[iplist], ipv6, addrem, ip_str, targets[iplist]);
}

static int
c2b_catch(void *arg, int proto, char* data, int size) {

    spin_log(LOG_DEBUG, "c2b_catch %x (%x, %x, %x) %d\n", proto, data[0], data[1], data[2], size);
    return 0;		// DROP
}

static void
setup_catch() {

    // Here we set up the catching of kernel messages for LOGed packets
    nfqroutine_register("core2block", c2b_catch, (void *) 0, QUEUE_BLOCK);
}

static void
wf_core2block(void *arg, int data, int timeout) {
    char buf[1024];

    if (timeout) {
	spin_log(LOG_DEBUG, "wf_core2block called\n");
	// TODO Do something with kernel messages
    }
}

void init_core2block() {
    static int all_lists[N_IPLIST] = { 1, 1, 1 };

    setup_catch();
    setup_debug();
    setup_tables();

    mainloop_register("core2block", wf_core2block, (void *) 0, 0, 10000);
    spin_register("core2block", c2b_changelist, (void *) 0, all_lists);
}

void cleanup_core2block() {
}
