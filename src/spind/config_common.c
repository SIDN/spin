#include <stdlib.h>
#include <string.h>

#include "spin_log.h"

enum configs {
    LOG_USESYSLOG,
    LOG_LOGLEVEL,
    PUBSUB_HOST,
    PUBSUB_PORT,
    PUBSUB_CHANNEL_TRAFFIC,
    PUBSUB_TIMEOUT,
    IPTABLE_QUEUE_DNS,
    IPTABLE_QUEUE_BLOCK,
    IPTABLE_PLACE_DNS,
    IPTABLE_PLACE_BLOCK,
    IPTABLE_DEBUG,
    NODE_CACHE_RETAIN_TIME,
    DOTS_ENABLED,   // Enable DOTS handler functionality
    DOTS_LOG_ONLY, // Only LOG DOTS mitigation request matches (do not block them)
};

struct conf_item {
    char *ci_name;
    char *ci_default;
    char *ci_value;
} ci_list[] = {
    [LOG_USESYSLOG] =
            { "log_usesyslog",              "1",                0   },
    [LOG_LOGLEVEL] =
            { "log_loglevel",               "6",                0   },
    [PUBSUB_HOST] =
            { "pubsub_host",                "127.0.0.1",        0   },
    [PUBSUB_PORT] =
            { "pubsub_port",                "1883",             0   },
    [PUBSUB_CHANNEL_TRAFFIC] =
            { "pubsub_channel_traffic",     "SPIN/traffic",     0   },
    [PUBSUB_TIMEOUT] =
            { "pubsub_timeout",             "60",               0   },
    [IPTABLE_QUEUE_DNS] =
            { "iptable_queue_dns",          "1",                0   },
    [IPTABLE_QUEUE_BLOCK] =
            { "iptable_queue_block",        "2",                0   },
    [IPTABLE_PLACE_DNS] =
            { "iptable_place_dns",          "0",                0   },
    [IPTABLE_PLACE_BLOCK] =
            { "iptable_place_block",        "0",                0   },
    [IPTABLE_DEBUG] =
            { "iptable_debug",              "/tmp/block_commands",                 0   },
    [NODE_CACHE_RETAIN_TIME] =
            { "node_cache_retain_time",     "1800",             0   },
    [DOTS_ENABLED] =
            { "dots_enabled",               "0",                0   },
    [DOTS_LOG_ONLY] =
            { "dots_log_only",              "0",                0   },
 { 0, 0, 0 }
};

void
config_set_option(char *name, char *value) {
    struct conf_item *p;

    // printf("Set option %s to value '%s'\n", name, value);
    for (p=ci_list; p->ci_name != 0; p++) {
        if (strcmp(p->ci_name, name)==0) {
            p->ci_value = value;
            return;
        }
    }
    spin_log(LOG_ERR, "Found unknown option %s, value %s\n", name, value);
}

void
init_config() {
    struct conf_item *p;
    int get_config_entries();

#ifdef notdef
    char *s;
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
    // printf("spi_int(%d) = %d('%s')\n", n, val, ci_list[n].ci_value);
    return val;
}

static char *
spi_str(int n) {
    char *val;

    val = ci_list[n].ci_value;
    // printf("spi_str(%d) = '%s'\n", n, val);
    return val;
}

int spinconfig_log_usesyslog() {

    return (spi_int(LOG_USESYSLOG));
}

int spinconfig_log_loglevel() {

    return (spi_int(LOG_LOGLEVEL));
}

char *spinconfig_pubsub_host() {

    return(spi_str(PUBSUB_HOST));
}

int spinconfig_pubsub_port() {

    return(spi_int(PUBSUB_PORT));
}

char *spinconfig_pubsub_channel_traffic() {

    return(spi_str(PUBSUB_CHANNEL_TRAFFIC));
}

int spinconfig_pubsub_timeout() {

    return(spi_int(PUBSUB_TIMEOUT));
}

int spinconfig_iptable_nflog_dns_group() {

    return(spi_int(IPTABLE_QUEUE_DNS));
}

int spinconfig_iptable_queue_block() {

    return(spi_int(IPTABLE_QUEUE_BLOCK));
}

int spinconfig_iptable_place_dns() {

    return(spi_int(IPTABLE_PLACE_DNS));
}

int spinconfig_iptable_place_block() {

    return(spi_int(IPTABLE_PLACE_BLOCK));
}

char *spinconfig_iptable_debug() {

    return(spi_str(IPTABLE_DEBUG));
}

int spinconfig_node_cache_retain_time() {
    return(spi_int(NODE_CACHE_RETAIN_TIME));
}

int spinconfig_dots_enabled() {
    return(spi_int(DOTS_ENABLED));
}

int spinconfig_dots_log_only() {
    return(spi_int(DOTS_LOG_ONLY));
}
