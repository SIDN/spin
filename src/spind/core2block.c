#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "mainloop.h"

#define KERNLOG	"/tmp/xx"
FILE *logfile;	

#define MAXSTR 1024

void iptab_system(char *s) {
    int result;

    printf("TEST: %s\n", s);
    return;
    result = system(s);
    assert (result == 0);
}

void iptab_make_table(char *name) {
    char str[MAXSTR];

    sprintf(str, "iptables -N %s", name);
    iptab_system(str);
}

void iptab_add_jump(char *table, int insert, char *cond, char *dest) {
    char str[MAXSTR];

    sprintf(str, "iptables -%s %s%s%s -j %s", insert ? "I" : "A", table,
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

char *list_block[] = {
    "8.8.4.4",
    0
};

void testmain() {
    char **p;

    iptab_make_table(SpinCheck);
    iptab_add_jump(table_input, 0, 0, SpinCheck);
    iptab_add_jump(table_output, 0, 0, SpinCheck);
    iptab_add_jump(table_forward, 0, 0, SpinCheck);

    iptab_make_table(SpinLog);
    iptab_add_jump(SpinLog, 0, 0, "LOG --log-prefix \"Spin blocked\"");

    iptab_make_table(SpinBlock);
    iptab_add_jump(SpinBlock, 0, 0, SpinLog);
    iptab_add_jump(SpinBlock, 0, 0, "DROP");

    for (p = list_block; *p; p++) {
	iptab_add_jump(SpinCheck, 0, iptab_cond(1, *p), SpinBlock);
	iptab_add_jump(SpinCheck, 0, iptab_cond(0, *p), SpinBlock);
    }
}

void wf_core2block(void *arg, int data, int timeout) {
    char buf[1024];

    if (timeout) {
	while (fgets(buf, 1024, logfile)) {
	    fprintf(stdout, "read: %s", buf);
	}
    }
}

void init_core2block() {

    logfile = fopen(KERNLOG, "r");
    fseek(logfile, 0, SEEK_END);

    mainloop_register("core2block", wf_core2block, (void *) 0, 0, 1000);
}

void cleanup_core2block() {
}
