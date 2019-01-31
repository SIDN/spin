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

#define MAXSTR 1024

FILE *logfile;

void setup_debug() {

    logfile = fopen("/tmp/block_commands", "w");
    setbuf(logfile, NULL);
}

void iptab_system(char *s) {
    int result;

    fprintf(logfile, "%s\n", s);
    return;
    result = system(s);
    assert (result == 0);
}

void iptab_make_table(char *name) {
    char str[MAXSTR];

    sprintf(str, "iptables -N %s", name);
    iptab_system(str);
    sprintf(str, "ip6tables -N %s", name);
    iptab_system(str);
}

void iptab_add_jump(char *table, int insert, char *cond, char *dest) {
    char str[MAXSTR];

    sprintf(str, "iptables -%s %s%s%s -j %s", insert ? "I" : "A", table,
    			cond ? " " : "", cond ? cond : "", dest);
    iptab_system(str);
    sprintf(str, "ip6tables -%s %s%s%s -j %s", insert ? "I" : "A", table,
    			cond ? " " : "", cond ? cond : "", dest);
    iptab_system(str);
}

static char condstr[MAXSTR];
char *iptab_cond(int src, char *addr) {

    sprintf(condstr, "%s %s", src ? "-s" : "-d", addr);
    return condstr;
}

char *table_input = "INPUT";
char *table_output = "OUTPUT";
char *table_forward = "FORWARD";

char *SpinCheck = "SpinCheck";
char *SpinBlock = "SpinBlock";
char *SpinLog = "SpinLog";

static void
setup_tables() {
    char **p;

    iptab_make_table(SpinCheck);
    iptab_add_jump(table_input, 1, 0, SpinCheck);
    iptab_add_jump(table_output, 1, 0, SpinCheck);
    iptab_add_jump(table_forward, 1, 0, SpinCheck);

    iptab_make_table(SpinLog);
    iptab_add_jump(SpinLog, 0, 0, "LOG --log-prefix \"Spin blocked\"");

    iptab_make_table(SpinBlock);
    iptab_add_jump(SpinBlock, 0, 0, SpinLog);
    iptab_add_jump(SpinBlock, 0, 0, "DROP");
}

char *iptables_command[2] = { "iptables", "ip6tables" };
char *srcdst[2] = { "-s", "-d" };

static void
c2b_do_block(int ipv6, int add, char *ip_str) {
    char *cmd, *flag;
    char str[MAXSTR];
    int i;

    cmd = iptables_command[ipv6];
    flag = add ? "-I": "-D";
    for (i=0; i<2; i++) {
	sprintf(str, "%s %s %s %s -j %s", cmd, flag, SpinCheck, ip_str, SpinBlock);
	iptab_system(str);
    }
}

static void
c2b_do_ignore(int ipv6, int add, char *ip_str) {
    char *cmd, *flag;
    char str[MAXSTR];
    int i;

    cmd = iptables_command[ipv6];
    flag = add ? "-I": "-D";
    for (i=0; i<2; i++) {
	sprintf(str, "%s %s %s %s %s -j %s", cmd, flag, SpinLog, srcdst[i], ip_str, "RETURN");
	iptab_system(str);
    }
}

static void
c2b_do_allow(int ipv6, int add, char *ip_str) {
    char *cmd, *flag;
    char str[MAXSTR];
    int i;

    cmd = iptables_command[ipv6];
    flag = add ? "-I": "-D";
    for (i=0; i<2; i++) {
	sprintf(str, "%s %s %s %s %s -j %s", cmd, flag, SpinBlock, srcdst[i], ip_str, "return");
	iptab_system(str);
    }
}

void c2b_changelist(int iplist, int add, ip_t *ip_addr) {
    int ipv6 = 0;
    char ip_str[INET6_ADDRSTRLEN];


    // IP v4 or 6, decode address
    if (ip_addr->family != AF_INET)
	ipv6 = 1;
    spin_ntop(ip_str, ip_addr, INET6_ADDRSTRLEN);
    // spin_log(LOG_DEBUG, "Change list %d %d %d %s\n", iplist, add, ipv6, ip_str);
    switch (iplist) {
    case IPLIST_BLOCK:
	c2b_do_block(ipv6, add, ip_str);
	break;
    case IPLIST_IGNORE:
	c2b_do_ignore(ipv6, add, ip_str);
	break;
    case IPLIST_ALLOW:
	c2b_do_allow(ipv6, add, ip_str);
	break;
    }
}


void setup_catch() {

    // Here we set up the catching of kernel messages for LOGed packets
}

void wf_core2block(void *arg, int data, int timeout) {
    char buf[1024];

    if (timeout) {
	spin_log(LOG_DEBUG, "wf_core2block called\n");
#ifdef notdef
	while (fgets(buf, 1024, logfile)) {
	    fprintf(stdout, "read: %s", buf);
	}
#endif
    }
}

void init_core2block() {

    setup_catch();
    setup_debug();
    setup_tables();

    mainloop_register("core2block", wf_core2block, (void *) 0, 0, 10000);
}

void cleanup_core2block() {
}
