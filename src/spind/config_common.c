#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "spin_log.h"
#include "spinconfig.h"

struct conf_item {
    char *ci_name;
    char *ci_default;
    char *ci_value;
} ci_list[] = {

/*  0 */    { "log_usesyslog",              "1",                0   },
/*  1 */    { "log_loglevel",               "6",                0   },
/*  2 */    { "pubsub_host",                "127.0.0.1",        0   },
/*  3 */    { "pubsub_port",                "1883",             0   },
/*  4 */    { "pubsub_channel_commands",    "SPIN/commands",    0   },
/*  5 */    { "pubsub_channel_traffic",     "SPIN/traffic",     0   },
/*  6 */    { "pubsub_timeout",             "60",               0   },
/*  7 */    { "iptable_queue_dns",          "1",                0   },
/*  8 */    { "iptable_queue_block",        "2",                0   },
/*  9 */    { "iptable_place_dns",          "0",                0   },
/* 10 */    { "iptable_place_block",        "0",                0   },
/* 11 */    { "iptable_debug",              "",                 0   },
 { 0, 0, 0 }
};


void
config_set_option(char *name, char *value) {
    struct conf_item *p;

    for (p=ci_list; p->ci_name != 0; p++) {
        if (strcmp(p->ci_name, name)==0) {
            p->ci_value = value;
            // fprintf(stderr, "Found option %s with value %s\n", name, value);
            return;
        }
    }
    spin_log(LOG_ERR, "Found unknown option %s, value %s\n", name, value);
}

void
init_config() {
    struct conf_item *p;
    char *s;
    int get_config_entries();

#ifdef notdef
    char *get_config_entry();
    for (p=ci_list; p->ci_name != 0; p++) {
        s = get_config_entry(p->ci_name);
        if (s == 0) {
            s = p->ci_default;
        }
        p->ci_value = s;
    }
#endif

    if (get_config_entries()) {
        spin_log(LOG_ERR, " init_config GCE failed\n");
    }

    // Set rest to default
    for (p=ci_list; p->ci_name != 0; p++) {
        if (p->ci_value == 0) {
            p->ci_value = p->ci_default;
        }
    }
}

static int
spi_int(int n) {
    int val;

    val = atoi(ci_list[n].ci_value);
    printf("spi_int(%d) = %d(%s)\n", n, val, ci_list[n].ci_value);
    return val;
}

static char *
spi_str(int n) {
    char *val;

    val = ci_list[n].ci_value;
    printf("spi_str(%d) = %s\n", n, val);
    return val;
}

int spinconfig_log_usesyslog() {

    return (spi_int(0));
}

int spinconfig_log_loglevel() {

    return (spi_int(1));
}

char *spinconfig_pubsub_host() {

    return(spi_str(2));
}

int spinconfig_pubsub_port() {

    return(spi_int(3));
}

char *spinconfig_pubsub_channel_commands() {

    return(spi_str(4));
}

char *spinconfig_pubsub_channel_traffic() {

    return(spi_str(5));
}

int spinconfig_pubsub_timeout() {

    return(spi_int(6));
}

int spinconfig_iptable_queue_dns() {

    return(spi_int(7));
}

int spinconfig_iptable_queue_block() {

    return(spi_int(8));
}

int spinconfig_iptable_place_dns() {

    return(spi_int(9));
}

int spinconfig_iptable_place_block() {

    return(spi_int(10));
}

char *spinconfig_iptable_debug() {

    return(spi_str(11));
}

